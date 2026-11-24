"""Exercise admission, exact-variant intake and fail-closed physical results offline."""
from contextlib import nullcontext
from hashlib import sha256
import io
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import time
import unittest
from unittest.mock import patch
from uuid import uuid4

from monitor import EvidenceError
from release_admission import require_admission
from release_health import NO_HAT, OssmMonitor, PRODUCTION_MQTT, ReadinessNotReached, StartupTimeout, WITH_HAT
import release_queue
from release_queue import (Blocked, HatSwitchError, LeaseLost, ReleaseFamilies, RestoreObserver, Worker,
                           allows_larger_flash, enrolled_devices, matching_board, switch_hat, verify_board)


DIGEST = "a"*64
SHA = "b"*40
PULL = {"source":"pull_request", "jobId":str(uuid4()), "leaseToken":str(uuid4()),
        "repository":"researchanddesire/OSSM", "runId":123, "runAttempt":1, "buildSha":SHA}


class Queue:
    def __init__(self, job):
        self.job = job
        self.requests = []
        self.admitted = False
        self.finished = False

    def request(self, route, body):
        self.requests.append(body)
        action = body["action"]
        if action == "claim":
            return {"job": self.job}
        if self.finished or (action == "authorize-pr" and not self.admitted):
            raise LeaseLost()
        return {"ok":True}


class Bench:
    def __init__(self):
        self.busy = False
        self.reads = []

    def request(self, route):
        self.reads.append(route)
        if route != "/status":
            raise AssertionError("Admission cannot start physical operations")
        return {"operator":None, "jobs":[], "devices":[], "activeRun":"occupied" if self.busy else None}


class QueueTests(unittest.TestCase):
    def setUp(self):
        temp = TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.home = Path(temp.name)
        (self.home/"release-admission").mkdir()
        (self.home/"release-admission"/"enabled").touch()
        self.bench = Bench()
        self.q = Queue(PULL)
        self.worker = Worker(self.q, self.bench, self.home, "rig")
        self.enrollment = patch("release_queue.enrolled_devices", return_value={})
        self.enrollment.start()
        self.addCleanup(self.enrollment.stop)

    def test_pr_fencing_after_restart_and_completion(self):
        self.worker.tick()
        self.assertFalse(self.worker.receipt.exists())
        restored = Worker(self.q, self.bench, self.home, "rig")
        restored.tick()
        self.assertFalse(restored.receipt.exists())
        self.q.admitted = True
        restored.tick()
        admission = {k: PULL[k] for k in ("runId","runAttempt","repository","buildSha")}
        require_admission(restored.receipt, admission, timeout=0)
        self.assertNotIn("leaseToken", restored.receipt.read_text())
        with self.assertRaises(SystemExit):
            require_admission(restored.receipt, dict(admission,runAttempt=2), timeout=0)
        self.q.finished = True
        restored.tick()
        self.assertFalse(restored.state.exists())
        self.assertFalse(restored.receipt.exists())
        self.assertEqual(self.bench.reads, ["/status"])
        self.assertEqual(sum(x["action"] == "claim" for x in self.q.requests),1)

    def test_busy_rig_does_not_claim_and_empty_rig_polls(self):
        self.bench.busy = True
        self.worker.tick()
        self.assertEqual(self.q.requests, [])
        self.bench.busy = False
        self.q.job = None
        self.worker.tick()
        self.assertEqual(self.q.requests, [{"runner":"rig","action":"claim","capabilities":[]}])

    def test_no_claimable_lanes_before_cutover(self):
        self.worker.receipt.with_name("enabled").unlink()
        self.q.job = None
        self.worker.tick()
        self.assertEqual(self.q.requests[-1]["capabilities"], [])
        self.assertFalse(self.worker.state.exists())

    def test_completed_result_is_retried_without_restarting_hardware(self):
        job = dict(source="merge",jobId=str(uuid4()),releaseId=str(uuid4()),leaseToken=str(uuid4()),
                   buildSha=SHA,deviceType="ossm",hardwareVariant="v2")
        self.worker.remember({"job":job,"result":["blocked",{"detail":"Fixture needs service"}]})
        self.worker.tick()
        self.assertEqual([r["action"] for r in self.q.requests], ["complete"])
        self.assertEqual(self.q.requests[0]["summary"], {"detail":"Fixture needs service"})
        self.assertFalse(self.worker.state.exists())

    def test_stale_receipt_blocks(self):
        self.worker.receipt.write_text(json.dumps({**PULL, "expiresAt": time.time()-1}))
        with self.assertRaises(SystemExit):
            require_admission(self.worker.receipt, {"buildSha":SHA}, timeout=0)

    def test_connected_enrollment_and_reprobe(self):
        fixture = dict(device_id="DEADBEEF0001",flash_bytes=16<<20,chip="esp32",
                       usb_vid=123,usb_pid=456,usb_location="1-2")
        board = dict(connected=True, family="unknown",vid=123,pid=456,location="1-2:1.0",
                     identity={"macAddress":"DE:AD:BE:EF:00:01","flashSizeBytes":16<<20,"chipFamily":"ESP32"})
        self.assertEqual(matching_board([board],"ossm",fixture),board)
        self.assertIsNone(matching_board([board,board],"ossm",fixture))
        self.assertIsNone(matching_board([dict(board,family="dtt")],"ossm",fixture))
        verify_board(board,fixture)
        with self.assertRaises(Blocked):
            verify_board(board,dict(fixture,flash_bytes=4<<20))

    def test_release_queue_to_qualified_lockbox_and_missing_telemetry(self):
        image = bytearray(64)
        image[0] = 0xe9
        image.extend(b" ".join(b'LKBX_HEALTH {"event":"' + event + b'"' for event in
                               (b"boot", b"heartbeat", b"mqtt_connected", b"wifi_connected")))
        job = {"source":"merge", "kind":"firmware", "releaseId":str(uuid4()),"jobId":str(uuid4()),
               "leaseToken":str(uuid4()),"version":"2.0.0","buildSha":SHA,"deviceType":"lkbx",
               "hardwareVariant":"r2","track":"main"}
        usb = dict(id="board",label="test",connected=True,family="lkbx",vid=123,pid=456,location="1-2",monitoring=False,
                   identity=dict(macAddress="AA:BB:CC:DD:EE:FF",flashSizeBytes=16<<20,chipFamily="ESP32-S3",lkbxVariant="r2"))
        fixture = dict(device_id="AABBCCDDEEFF",flash_bytes=16<<20,chip="esp32s3",psram="r2",
                       usb_vid=123,usb_pid=456,usb_location="1-2",product="lockbox")
        self.enrollment.stop()
        with patch("release_queue.enrolled_devices",return_value={("lkbx","r2"):fixture}), \
             patch("release_queue.rig_lock",return_value=nullcontext()),patch("release_queue.find_port",return_value="TEST"):
            for available in (True, False):
                data = bytes(image if available else image[:64])
                digest = sha256(data).hexdigest()
                job["artifacts"] = [dict(role="application",filename="firmware.bin",sha256=digest,sizeBytes=len(data),
                                         url=f"https://acjajruwevyyatztbkdf.supabase.co/storage/v1/object/public/lkbx-firmware/releases/2.0.0/{SHA}/r2/firmware.bin")]
                self.q.job = job
                recorded = []
                def status(route):
                    return {"operator":None,"devices":[usb],"activeRun":None,"fixtureValidationProtocol":1,
                            "jobs":[dict(id="local-job",status="completed",verifiedApplicationSha256=digest)]}
                def command(action, **args):
                    recorded.append(action)
                    return {"id":"local-job"}
                def observe_fixture(device, port, monitor, log, guard):
                    def send(seconds, event, **kwargs):
                        monitor.feed("LKBX_HEALTH "+json.dumps(dict(event=event,boot_id=5,uptime_ms=seconds*1000,**kwargs)),
                                     monitor.started+seconds)
                    send(1,"boot", **{k:monitor.expected[k] for k in ("variant","device_id","track","build_sha","image_sha256")})
                    send(2,"wifi_connected",ip="192.168.1.3")
                    send(3,"mqtt_connected",broker=PRODUCTION_MQTT)
                    for i in range(5,186,5):
                        send(i,"heartbeat",main_menu=True,menu_age_ms=10)
                # The stub delivers its final heartbeat at exactly 180s. Use a
                # whole-second epoch so that rounding cannot shorten the simulated soak.
                with patch("release_queue.time.monotonic", return_value=1_000.0), \
                     patch.object(self.bench,"request",side_effect=status), \
                     patch.object(self.bench,"command",create=True,side_effect=command), \
                     patch("release_queue.build_opener") as http, patch("release_queue.observe",side_effect=observe_fixture):
                    http.return_value.open.return_value = io.BytesIO(data)
                    self.worker.tick()
                result=self.q.requests[-1]
                self.assertEqual(result["action"],"complete")
                self.assertEqual(result["status"],"success" if available else "blocked")
                self.assertEqual("firmware.flash" in recorded,available)
                if available:
                    self.assertEqual(result["summary"]["applicationSha256"],digest)
                    soak = result["summary"]["checks"][-1]
                    self.assertEqual(soak["evidenceCount"], 37)
                    self.assertEqual(soak["reason"], "180s host time, 180s device time")
                else:
                    self.assertEqual(recorded,[])


OSSM_ID = "4CC3821011DC"
OSSM = dict(product="ossm", enrolled=True, chip="esp32", flash_bytes=16<<20, psram=None, device_id=OSSM_ID,
            usb_vid=4292, usb_pid=60000, usb_location="1-4.3.4.2", allow_larger_flash=True)
DTT_V1 = dict(product="dtt", enrolled=True, chip="esp32", flash_bytes=16<<20, psram=None, device_id="1CC3ABB1C44C",
              usb_vid=4292, usb_pid=60000, usb_location="1-4.3.3", allow_larger_flash=True)
DTT_V2 = dict(DTT_V1, device_id="142B2FF69F98", usb_location="1-4.4")
del DTT_V2["allow_larger_flash"]


class Lanes(unittest.TestCase):
    def enroll(self, products):
        with TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.json"
            path.write_text(json.dumps({"schema": 1, "products": products}))
            return enrolled_devices(path)

    def test_one_16mb_ossm_serves_both_release_lanes(self):
        targets = self.enroll({"ossm": {"16mb": OSSM}, "dtt": {"v1": DTT_V1, "v2": DTT_V2}})
        self.assertEqual(sorted(targets), [("dtt", "v1"), ("dtt", "v2"), ("ossm", "v1"), ("ossm", "v2")])
        self.assertIs(targets[("ossm", "v1")], targets[("ossm", "v2")])
        single = {k: v for k, v in OSSM.items() if k != "allow_larger_flash"}
        self.assertEqual(sorted(self.enroll({"ossm": {"16mb": single}})), [("ossm", "v2")])

    def test_only_the_smaller_lane_asks_for_the_larger_chip(self):
        self.assertTrue(allows_larger_flash(OSSM, "ossm", "v1"))
        self.assertFalse(allows_larger_flash(OSSM, "ossm", "v2"))
        self.assertTrue(allows_larger_flash(DTT_V1, "dtt", "v1"))
        self.assertFalse(allows_larger_flash(DTT_V2, "dtt", "v2"))
        self.assertFalse(allows_larger_flash(dict(OSSM, allow_larger_flash=False), "ossm", "v1"))


class FakePort:
    def __init__(self, replies):
        self.replies = replies
        self.written = []
        self.buffer = bytearray()

    @property
    def in_waiting(self):
        return len(self.buffer)

    def write(self, data):
        self.written.append(data)
        self.buffer.extend(self.replies(data.decode()))

    def read(self, size):
        data, self.buffer = bytes(self.buffer[:size]), self.buffer[size:]
        return data

    def close(self):
        pass


class Clock:
    def __init__(self):
        self.now = 0.0

    def __call__(self):
        self.now += 0.5
        return self.now


class HatSwitch(unittest.TestCase):
    def test_waits_for_the_acknowledgement_and_homing(self):
        def replies(command):
            mode = command.split()[-1]
            return (f'RAD_HAT {{"mode":"{mode}","applies":"next_boot"}}\n'
                    'OSSM_HEALTH {"event":"heartbeat","boot_id":1,"uptime_ms":9,"homed":true}\n').encode()
        port = FakePort(replies)
        self.assertTrue(switch_hat(port, "off", clock=Clock()))
        self.assertEqual(port.written, [b"\nhat off\n"])

    def test_a_wrong_or_missing_reply_is_not_an_acknowledgement(self):
        port = FakePort(lambda command: b'RAD_HAT {"mode":"auto","applies":"next_boot"}\n')
        with self.assertRaises(HatSwitchError):
            switch_hat(port, "off", clock=Clock(), ack_seconds=10)
        self.assertGreater(len(port.written), 1)  # resent while unanswered

    def test_settles_without_homing_after_the_limit(self):
        port = FakePort(lambda command: b'RAD_HAT {"mode":"auto","applies":"next_boot"}\n')
        self.assertFalse(switch_hat(port, "auto", clock=Clock(), settle_seconds=30))
        self.assertEqual(len(port.written), 1)


class HatRefusals(unittest.TestCase):
    def test_a_refused_write_is_retried_until_the_guard_disarms(self):
        replies = iter([b'RAD_HAT {"mode":"auto","applies":"next_boot","error":"motor_armed"}\n'] * 3 +
                       [b'RAD_HAT {"mode":"off","applies":"next_boot"}\n'])
        port = FakePort(lambda command: next(replies))
        self.assertFalse(switch_hat(port, "off", clock=Clock(), settle_seconds=40))
        self.assertEqual(len(port.written), 4)

    def test_a_write_still_refused_after_homing_is_reported(self):
        port = FakePort(lambda command: b'RAD_HAT {"mode":"auto","applies":"next_boot","error":"motor_armed"}\n')
        with self.assertRaisesRegex(HatSwitchError, "refused 'hat off' \\(motor_armed\\)"):
            switch_hat(port, "off", clock=Clock(), settle_seconds=30)

    def test_a_refusal_naming_the_requested_mode_is_already_stored(self):
        port = FakePort(lambda command: b'RAD_HAT {"mode":"auto","applies":"next_boot","error":"storage"}\n')
        self.assertFalse(switch_hat(port, "auto", clock=Clock(), settle_seconds=5))

    def test_the_plain_hat_report_is_not_a_reply(self):
        report = ('RAD_HAT {"detected":false,"fresh":false,"passive":false,"guardReady":true,"guardArmed":false,'
                  '"guardLatched":false,"outputDisabled":true,"guardFault":0,"mode":"off"}\n').encode()
        with self.assertRaises(HatSwitchError):
            switch_hat(FakePort(lambda command: report), "off", clock=Clock(), ack_seconds=5)

    def test_refusal_falls_back_to_the_window_before_homing(self):
        worker = Worker(Queue(None), Bench(), Path("."), "rig")
        calls = []
        def switch(port, mode, guard=None, **kwargs):
            calls.append(kwargs)
            if len(calls) == 1:
                raise HatSwitchError("The OSSM refused 'hat off' (motor_armed)")
            return True
        with patch("release_queue.find_port", return_value="COM6"), \
             patch("release_queue.open_console", return_value=FakePort(lambda c: b"")), \
             patch("release_queue.guard_ready") as ready, patch("release_queue.switch_hat", side_effect=switch), \
             patch("release_queue.reset_board") as reset:
            worker.prepare_case(OSSM, "off", FakeGuard(None, None), lambda: None)
        reset.assert_called_once()
        self.assertEqual(calls[1]["resend_seconds"], 0.5)
        self.assertEqual(ready.call_count, 3)  # before the console, before the reset, before the case's reset


class Classification(unittest.TestCase):
    def outcome(self, error, booted=True):
        observer = OssmMonitor({}, 0)
        observer.boot_id = 1 if booted else None
        with TemporaryDirectory() as directory, patch("release_queue.find_port", return_value="COM6"), \
             patch("release_queue.observe", side_effect=error):
            return Worker.observe_case({}, observer, None, Path(directory) / "serial.log")

    def test_readiness_not_reached_blocks_and_device_faults_fail(self):
        detail = "Readiness not reached in 180 s: production MQTT never connected (heartbeats continued)"
        self.assertEqual(self.outcome(ReadinessNotReached(detail)), ("blocked", detail))
        self.assertEqual(self.outcome(StartupTimeout("readiness"), booted=False)[0], "blocked")
        for fault in ("Lockbox reported a reset, panic, watchdog or brownout",
                      "Lockbox boot, hardware variant or running image differs from the candidate",
                      "Menu heartbeat stopped for over 20 seconds", "Lockbox device uptime stopped advancing",
                      "Lockbox menu task stopped making progress", "Lockbox reconnected during the observation",
                      "Repeated damaged OSSM health telemetry (6 records after boot)",
                      "Lockbox sent no heartbeat within 180 seconds of its boot record"):
            self.assertEqual(self.outcome(EvidenceError(fault)), ("failed", fault))


class FakeGuard:
    def __init__(self, path, output, *args):
        self.path = path
        self.failure = None
        self.closed = False

    def require(self, *args, **kwargs):
        pass

    def check(self):
        if self.failure:
            raise RuntimeError(self.failure)

    def close(self):
        self.closed = True


class OssmRelease(unittest.TestCase):
    """The With HAT and No HAT cases on the one OSSM, with the board, bench and guard faked."""
    def setUp(self):
        temp = TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.home = Path(temp.name)
        (self.home / "release-admission").mkdir()
        (self.home / "release-admission" / "enabled").touch()
        self.queue = Queue(None)
        self.worker = Worker(self.queue, Bench(), self.home, "rig", motion_devices=[OSSM_ID])

    def run_job(self, variant="v1", observe=None, guard_ready=None, image_events=(b"boot", b"heartbeat"), hat_switch=True):
        image = bytearray(64)
        image[0] = 0xe9
        image.extend(b" ".join(b'OSSM_HEALTH {"event":"' + event + b'"' for event in image_events))
        if hat_switch:
            image.extend(b' RAD_HAT applies next_boot')
        data = bytes(image)
        digest = sha256(data).hexdigest()
        job = {"source": "merge", "kind": "firmware", "releaseId": str(uuid4()), "jobId": str(uuid4()),
               "leaseToken": str(uuid4()), "version": "1.0.70", "buildSha": SHA, "deviceType": "ossm",
               "hardwareVariant": variant, "track": "main",
               "artifacts": [dict(role="application", filename="firmware.bin", sha256=digest, sizeBytes=len(data),
                                  url="https://acjajruwevyyatztbkdf.supabase.co/storage/v1/object/public/ossm-firmware/"
                                      f"releases/1.0.70/{SHA}/{variant}/firmware.bin")]}
        self.queue.job = job
        usb = dict(id="ossm-board", label="OSSM", connected=True, family="ossm", vid=4292, pid=60000,
                   location="1-4.3.4.2:x.0", monitoring=False,
                   identity=dict(macAddress="4C:C3:82:10:11:DC", flashSizeBytes=16<<20, chipFamily="ESP32"))
        self.commands, self.modes, self.observed, self.held = [], [], [], []
        def status(route):
            return {"operator": None, "devices": [usb], "activeRun": None, "fixtureValidationProtocol": 1,
                    "jobs": [dict(id="local-job", status="completed", verifiedApplicationSha256=digest)]}
        def command(action, **args):
            self.commands.append((action, args))
            return {"id": "local-job"}
        def switch(port, mode, guard=None, **kwargs):
            self.modes.append(mode)
            return True
        def watched(device, port, monitor, log, guard):
            self.observed.append(getattr(monitor, "case", None) if isinstance(monitor, OssmMonitor) else "restore")
            if isinstance(monitor, RestoreObserver):
                return restore(monitor)
            return (observe or passing)(monitor)
        with patch("release_queue.enrolled_devices", return_value={("ossm", "v1"): OSSM, ("ossm", "v2"): OSSM}), \
             patch("release_queue.rig_lock", return_value=nullcontext()), \
             patch("release_queue.find_port", return_value="COM6"), \
             patch("release_queue.GuardWatch", FakeGuard), \
             patch("release_queue.guard_ready", side_effect=guard_ready or (lambda guard: guard.check())), \
             patch("release_queue.open_console", return_value=FakePort(lambda c: b"")), \
             patch("release_queue.switch_hat", side_effect=switch), \
             patch("release_queue.hold_in_loader", side_effect=lambda port, log: self.held.append(port)), \
             patch("release_queue.time.monotonic", return_value=1_000.0), \
             patch.object(self.worker.bench, "request", side_effect=status), \
             patch.object(self.worker.bench, "command", create=True, side_effect=command), \
             patch("release_queue.build_opener") as http, patch("release_queue.observe", side_effect=watched):
            http.return_value.open.return_value = io.BytesIO(data)
            self.worker.tick()
        result = self.queue.requests[-1]
        self.assertEqual(result["action"], "complete")
        return result

    def test_v1_release_runs_both_cases_and_restores_hat_auto(self):
        result = self.run_job("v1")
        self.assertEqual(result["status"], "success", result["summary"]["detail"])
        flash = next(args for action, args in self.commands if action == "firmware.flash")["fixtureValidation"]
        self.assertEqual((flash["variant"], flash["allowLargerFlash"], flash["allowDttV1On16Mb"]), ("v1", True, True))
        self.assertEqual(self.modes, ["auto", "off", "auto"])
        self.assertEqual(self.observed, [WITH_HAT, NO_HAT, "restore"])
        titles = [check["title"] for check in result["summary"]["checks"]]
        self.assertEqual(titles[0], "Physical application SHA-256 readback")
        self.assertIn("With HAT: Continuous application and network observation", titles)
        self.assertIn("No HAT: HAT detection off, carriage homed on the board's current sensor", titles)
        self.assertEqual(titles[-1], "HAT detection restored for the next run")
        self.assertTrue(all(check["state"] == "passed" for check in result["summary"]["checks"]))
        self.assertEqual([c["outcome"] for c in result["summary"]["cases"]], ["success", "success"])

    def test_v2_release_does_not_ask_for_a_larger_chip(self):
        result = self.run_job("v2")
        self.assertEqual(result["status"], "success", result["summary"]["detail"])
        flash = next(args for action, args in self.commands if action == "firmware.flash")["fixtureValidation"]
        self.assertEqual((flash["variant"], flash["allowLargerFlash"], flash["allowDttV1On16Mb"]), ("v2", False, False))

    def test_a_failed_case_fails_the_job_and_still_restores(self):
        def observe(monitor):
            if monitor.case is NO_HAT:
                passing(monitor, hat=True)
            passing(monitor)
        result = self.run_job("v1", observe)
        self.assertEqual(result["status"], "failed")
        self.assertIn("No HAT: OSSM detected its HAT although HAT detection was off", result["summary"]["detail"])
        self.assertEqual(self.modes, ["auto", "off", "auto"])
        self.assertEqual(self.observed[-1], "restore")
        states = {check["title"]: check["state"] for check in result["summary"]["checks"]}
        self.assertEqual(states["With HAT: Continuous application and network observation"], "passed")
        self.assertEqual(states["No HAT: Continuous application and network observation"], "failed")

    def test_a_failed_first_case_still_runs_no_hat(self):
        def observe(monitor):
            if monitor.case is WITH_HAT:
                raise EvidenceError("OSSM lost its HAT during observation")
            passing(monitor)
        result = self.run_job("v2", observe)
        self.assertEqual(result["status"], "failed")
        self.assertEqual(self.observed, [WITH_HAT, NO_HAT, "restore"])

    def test_unready_guard_never_resets_and_parks_the_board(self):
        def guard_ready(guard):
            raise RuntimeError("RS-485 safety guard: the guard has latched the motor off")
        result = self.run_job("v1", guard_ready=guard_ready)
        self.assertEqual(result["status"], "blocked")
        self.assertEqual(self.observed, [])
        self.assertEqual(self.modes, [])
        self.assertEqual(self.held, ["COM6"])
        detail = result["summary"]["detail"]
        self.assertIn("HAT auto was not restored", detail)
        self.assertIn("ROM loader", detail)
        states = {check["title"]: check["state"] for check in result["summary"]["checks"]}
        self.assertEqual(states["No HAT: Running image, board, build SHA and track"], "blocked")
        self.assertEqual(states["HAT detection restored for the next run"], "blocked")

    def test_guard_fault_during_a_case_skips_the_rest_and_parks_the_board(self):
        def observe(monitor):
            raise RuntimeError("RS-485 safety guard: guard stopped the motor: contact too long")
        calls = []
        def guard_ready(guard):
            calls.append(guard)
            if len(calls) > 2:
                raise RuntimeError("RS-485 safety guard: guard stopped the motor")
        result = self.run_job("v1", observe, guard_ready)
        self.assertEqual(result["status"], "blocked")
        self.assertEqual(self.observed, [WITH_HAT])
        self.assertEqual(self.held, ["COM6"])
        self.assertEqual([c["outcome"] for c in result["summary"]["cases"]], ["blocked", "blocked"])

    def test_restore_failure_blocks_an_otherwise_passing_job(self):
        unanswered = "The OSSM did not acknowledge the serial command 'hat auto'"
        with patch.object(release_queue.Worker, "restore_hat", lambda *args: unanswered):
            result = self.run_job("v1")
        self.assertEqual(result["status"], "blocked")
        self.assertIn("HAT auto was not restored", result["summary"]["detail"])
        restored = result["summary"]["checks"][-1]
        self.assertEqual((restored["state"], restored["reason"]), ("blocked", unanswered))

    def test_ossm_without_the_hat_switch_or_health_line_is_blocked_before_flashing(self):
        for events, switch in (((b"boot", b"heartbeat"), False), ((b"boot",), True)):
            result = self.run_job("v1", image_events=events, hat_switch=switch)
            self.assertEqual(result["status"], "blocked")
            self.assertNotIn("firmware.flash", [action for action, _ in self.commands])
            self.assertEqual(self.observed, [])


def passing(monitor, hat=None):
    case = monitor.case
    def send(seconds, event, **kwargs):
        monitor.feed("OSSM_HEALTH " + json.dumps(dict(event=event, boot_id=5, uptime_ms=seconds * 1000, **kwargs)),
                     monitor.started + seconds)
    send(1, "boot", hat_mode=case.mode, **{k: monitor.expected[k] for k in
                                            ("variant", "device_id", "track", "build_sha", "image_sha256")})
    send(2, "wifi_connected", ip="192.168.1.30")
    send(3, "mqtt_connected", broker=PRODUCTION_MQTT)
    for i in range(5, 186, 5):
        send(i, "heartbeat", main_menu=True, menu_age_ms=10, hat=case.hat if hat is None else hat, homed=True)


def restore(observer):
    def send(seconds, event, **kwargs):
        observer.feed("OSSM_HEALTH " + json.dumps(dict(event=event, boot_id=9, uptime_ms=seconds * 1000, **kwargs)),
                      observer.started + seconds)
    send(1, "boot", hat_mode="auto", device_id=observer.expected["device_id"])
    send(5, "heartbeat", homed=True)
    assert observer.passed


class ReleaseFamilyTests(unittest.TestCase):
    """release-families.json: the operator pauses release jobs per product."""
    def setUp(self):
        temp = TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.home = Path(temp.name)
        (self.home / "release-admission").mkdir()
        (self.home / "release-admission" / "enabled").touch()
        self.path = self.home / "release-families.json"

    def load(self, text=None, raw=None):
        if raw is not None:
            self.path.write_bytes(raw)
        elif text is not None:
            self.path.write_text(text, encoding="utf-8")
        return ReleaseFamilies.load(self.path)

    def test_absent_file_allows_every_family(self):
        families = self.load()
        self.assertTrue(all(families.refusal(f) is None for f in ("lkbx", "dtt", "ossm", "radr")))

    def test_listed_families_run_and_others_are_paused(self):
        families = self.load('["radr"]')
        self.assertIsNone(families.refusal("radr"))
        for family, name in (("lkbx", "Lockbox"), ("dtt", "DTT"), ("ossm", "OSSM")):
            self.assertEqual(families.refusal(family),
                             f"Release testing for {name} is paused on this rig (operator setting)")
        self.assertIsNotNone(self.load("[]").refusal("radr"))
        # Windows PowerShell's UTF-8 adds a byte order mark.
        self.assertIsNone(self.load(raw=b'\xef\xbb\xbf["dtt", "radr"]').refusal("dtt"))

    def test_an_invalid_file_pauses_every_family(self):
        for text in ('["radar"]', '"radr"', '{"radr": true}', '[1]', '["RADR"]', '["radr",', ''):
            with self.subTest(text=text):
                families = self.load(text)
                for family in ("lkbx", "dtt", "ossm", "radr"):
                    refusal = families.refusal(family)
                    self.assertTrue(refusal and refusal.startswith("Release testing is paused on this rig: "
                                                                   "release-families.json"), refusal)
        self.assertIn('must be a JSON array of "lkbx", "dtt", "ossm", "radr"', self.load('["radar"]').refusal("radr"))
        self.assertIn("could not be read (JSONDecodeError)", self.load("[").refusal("radr"))

    def test_a_paused_job_is_blocked_before_any_lock_download_or_bench_command(self):
        job = {"source": "merge", "kind": "firmware", "releaseId": str(uuid4()), "jobId": str(uuid4()),
               "leaseToken": str(uuid4()), "version": "1.0.108", "buildSha": SHA, "deviceType": "dtt",
               "hardwareVariant": "v2", "track": "main", "artifacts": []}
        queue, bench = Queue(job), Bench()  # this Bench fails on any command or non-status read
        worker = Worker(queue, bench, self.home, "rig", release_families=self.load('["radr"]'))
        fixture = dict(DTT_V2, product="dtt")
        with patch("release_queue.enrolled_devices", return_value={("dtt", "v2"): fixture}), \
             patch("release_queue.rig_lock") as lock, patch("release_queue.build_opener") as http, \
             patch("release_queue.observe") as observe:
            worker.tick()
        result = queue.requests[-1]
        self.assertEqual((result["action"], result["status"]), ("complete", "blocked"))
        self.assertEqual(result["summary"], {"detail": "Release testing for DTT is paused on this rig (operator setting)"})
        lock.assert_not_called(); http.assert_not_called(); observe.assert_not_called()
        self.assertEqual(bench.reads, ["/status"])
        self.assertFalse(worker.state.exists())

    def test_pull_requests_and_lanes_are_unaffected(self):
        paused = self.load("[]")
        queue, bench = Queue(PULL), Bench()
        worker = Worker(queue, bench, self.home, "rig", release_families=paused)
        queue.admitted = True
        with patch("release_queue.enrolled_devices", return_value={}):
            worker.tick()
            worker.tick()
        self.assertTrue(worker.receipt.exists())
        worker.state.unlink()  # the PR's lease ended; the rig is idle again
        fixture = dict(DTT_V2, product="dtt")
        board = dict(connected=True, family="dtt", vid=4292, pid=60000, location="1-4.4", serial=None)
        bench.request = lambda route: {"operator": None, "jobs": [], "devices": [board], "activeRun": None}
        idle = Queue(None)
        worker = Worker(idle, bench, self.home, "rig", release_families=paused)
        with patch("release_queue.enrolled_devices", return_value={("dtt", "v2"): fixture}):
            worker.tick()
        self.assertEqual(idle.requests[-1]["capabilities"], [{"deviceType": "dtt", "hardwareVariant": "v2"}])


if __name__ == "__main__":
    unittest.main()
