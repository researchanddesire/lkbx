"""Exercise admission, exact-variant intake and fail-closed physical results offline."""
from contextlib import nullcontext
from datetime import datetime, timezone
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
RADR_ID = "A4CB8FC2DCB8"
RADR = dict(product="radr", enrolled=True, chip="esp32s3", flash_bytes=16<<20, psram="r8", device_id=RADR_ID,
            usb_vid=4292, usb_pid=60000, usb_serial="0001")


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
    def test_returns_as_soon_as_the_reply_names_the_mode(self):
        def replies(command):
            mode = command.split()[-1]
            return f'RAD_HAT {{"mode":"{mode}","applies":"next_boot"}}\n'.encode()
        port = FakePort(replies)
        self.assertIsNone(switch_hat(port, "off", clock=Clock()))
        self.assertEqual(port.written, [b"\nhat off\n"])

    def test_a_wrong_or_missing_reply_is_not_an_acknowledgement(self):
        port = FakePort(lambda command: b'RAD_HAT {"mode":"auto","applies":"next_boot"}\n')
        with self.assertRaises(HatSwitchError):
            switch_hat(port, "off", clock=Clock(), ack_seconds=10)
        self.assertGreater(len(port.written), 1)  # resent while unanswered

    def test_sends_repeatedly_until_the_console_answers(self):
        answers = iter([b"", b"", b'RAD_HAT {"mode":"auto","applies":"next_boot"}\n'])
        port = FakePort(lambda command: next(answers))
        self.assertIsNone(switch_hat(port, "auto", clock=Clock(), resend_seconds=0.5))
        self.assertEqual(len(port.written), 3)


class HatRefusals(unittest.TestCase):
    def test_a_refused_write_is_retried_until_the_guard_disarms(self):
        replies = iter([b'RAD_HAT {"mode":"auto","applies":"next_boot","error":"motor_armed"}\n'] * 3 +
                       [b'RAD_HAT {"mode":"off","applies":"next_boot"}\n'])
        port = FakePort(lambda command: next(replies))
        self.assertIsNone(switch_hat(port, "off", clock=Clock(), refused_seconds=40))
        self.assertEqual(len(port.written), 4)

    def test_a_write_still_refused_after_homing_is_reported(self):
        port = FakePort(lambda command: b'RAD_HAT {"mode":"auto","applies":"next_boot","error":"motor_armed"}\n')
        with self.assertRaisesRegex(HatSwitchError, "refused 'hat off' \\(motor_armed\\)"):
            switch_hat(port, "off", clock=Clock(), refused_seconds=30)

    def test_a_refusal_naming_the_requested_mode_is_already_stored(self):
        port = FakePort(lambda command: b'RAD_HAT {"mode":"auto","applies":"next_boot","error":"storage"}\n')
        self.assertIsNone(switch_hat(port, "auto", clock=Clock()))

    def test_the_plain_hat_report_is_not_a_reply(self):
        report = ('RAD_HAT {"detected":false,"fresh":false,"passive":false,"guardReady":true,"guardArmed":false,'
                  '"guardLatched":false,"outputDisabled":true,"guardFault":0,"mode":"off"}\n').encode()
        with self.assertRaises(HatSwitchError):
            switch_hat(FakePort(lambda command: report), "off", clock=Clock(), ack_seconds=5)

    def test_a_setting_write_boot_is_parked_and_cleared_before_the_observed_boot(self):
        worker = Worker(Queue(None), Bench(), Path("."), "rig")
        steps = []
        guard = FakeGuard(None, None)
        guard.wait_session_clear = lambda: steps.append("wait clear")
        with TemporaryDirectory() as directory, patch("release_queue.find_port", return_value="COM6"), \
             patch("release_queue.open_console", return_value=FakePort(lambda c: b"")), \
             patch("release_queue.guard_ready", side_effect=lambda g: steps.append("guard ready")), \
             patch("release_queue.park", side_effect=lambda *a: steps.append("park")), \
             patch("release_queue.reset_board", side_effect=lambda *a: steps.append("boot to write hat")), \
             patch("release_queue.switch_hat", side_effect=lambda port, mode, guard: steps.append("hat " + mode)):
            worker.prepare_case(OSSM, "off", guard, lambda: steps.append("renew"), Path(directory) / "hold.log")
        self.assertEqual(steps, ["renew", "park", "wait clear", "guard ready", "boot to write hat", "hat off",
                                 "park", "wait clear", "guard ready"])


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
    timeline = None
    clear_failures = ()

    def __init__(self, path, output, *args):
        self.path = path
        self.failure = None
        self.closed = False
        self.waits = 0

    def wait_session_clear(self):
        self.waits += 1
        if FakeGuard.timeline is not None:
            FakeGuard.timeline.append("wait clear")
        if self.waits in FakeGuard.clear_failures:
            raise RuntimeError("RS-485 safety guard: its homing session did not clear within 30 s of parking "
                               "the OSSM (contacts: lower; carriage still)")

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

    def run_job(self, variant="v1", observe=None, guard_ready=None, image_events=(b"boot", b"heartbeat"), hat_switch=True,
                radar=True, radar_lock_held=False, radar_mac=RADR_ID, radar_fails=None, flash_error=None,
                park_error=None, ossm_monitored=False, clear_failures=(), contacts=("lower", "upper")):
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
                   location="1-4.3.4.2:x.0", monitoring=ossm_monitored,
                   identity=dict(macAddress="4C:C3:82:10:11:DC", flashSizeBytes=16<<20, chipFamily="ESP32"))
        radar_board = dict(id="radar-board", label="RADR", connected=True, family="radr", vid=4292, pid=60000,
                           serial="0001", location="1-4.2", monitoring=True)
        self.commands, self.modes, self.observed, self.held = [], [], [], []
        # One timeline of every hardware step, to check the Radar hold brackets the OSSM.
        self.timeline, self.locks = [], []
        FakeGuard.timeline, FakeGuard.clear_failures = self.timeline, clear_failures
        self.addCleanup(setattr, FakeGuard, "timeline", None)
        status = self.home / "rs485-guard" / "status.json"
        status.parent.mkdir(exist_ok=True)
        status.write_text(json.dumps({"time": datetime.now(timezone.utc).isoformat(), "contacts": list(contacts)}))
        def status(route):
            return {"operator": None, "devices": [usb, radar_board], "activeRun": None, "fixtureValidationProtocol": 1,
                    "jobs": [dict(id="local-job", status="completed", verifiedApplicationSha256=digest)]}
        def command(action, **args):
            self.commands.append((action, args))
            self.timeline.append(action + (":radar" if args.get("deviceId") == "radar-board" else ""))
            return {"id": "local-job"}
        def switch(port, mode, guard=None, **kwargs):
            self.modes.append(mode)
            self.timeline.append("hat " + mode)
            return True
        def watched(device, port, monitor, log, guard):
            self.observed.append(getattr(monitor, "case", None) if isinstance(monitor, OssmMonitor) else "restore")
            self.timeline.append("observe")
            if isinstance(monitor, RestoreObserver):
                return restore(monitor)
            return (observe or passing)(monitor)
        def parked(device, spec, log):
            self.timeline.append(f"park {device['product']}")
            # A park failure is injected only for the final park, after the restore.
            if park_error and self.observed and self.observed[-1] == "restore":
                raise park_error
        def esptool(port, before, after, log, timeout=60):
            self.timeline.append(f"radar {before}/{after} on {port}")
            if radar_fails and radar_fails[0] == after:
                raise radar_fails[1]
            return radar_mac
        def lock(path, **kwargs):
            self.locks.append(Path(path).name)
            if radar_lock_held and Path(path).name == "radr.lock":
                raise RuntimeError("another process owns this physical fixture")
            return nullcontext()
        def port(device):
            return "COM4" if device is RADR else "COM6"
        targets = {("ossm", "v1"): OSSM, ("ossm", "v2"): OSSM}
        if radar:
            targets[("radr", "default")] = RADR
        def failing(action, **args):
            command(action, **args)
            if flash_error and action == "firmware.flash":
                raise flash_error
            return {"id": "local-job"}
        with patch("release_queue.enrolled_devices", return_value=targets), \
             patch("release_queue.rig_lock", side_effect=lock), \
             patch("release_queue.radar_esptool", side_effect=esptool), \
             patch("release_queue.park", side_effect=parked), \
             patch("release_queue.reset_board", side_effect=lambda *a: self.timeline.append("boot to write hat")), \
             patch("release_queue.find_port", side_effect=port), \
             patch("release_queue.GuardWatch", FakeGuard), \
             patch("release_queue.guard_ready", side_effect=guard_ready or (lambda guard: guard.check())), \
             patch("release_queue.open_console", return_value=FakePort(lambda c: b"")), \
             patch("release_queue.switch_hat", side_effect=switch), \
             patch("release_queue.hold_in_loader", side_effect=lambda port, log: self.held.append(port)), \
             patch("release_queue.time.monotonic", return_value=1_000.0), \
             patch.object(self.worker.bench, "request", side_effect=status), \
             patch.object(self.worker.bench, "command", create=True, side_effect=failing), \
             patch("release_queue.build_opener") as http, patch("release_queue.observe", side_effect=watched):
            http.return_value.open.return_value = io.BytesIO(data)
            self.worker.tick()
        result = self.queue.requests[-1]
        self.assertEqual(result["action"], "complete")
        return result

    HOLD = "radar default_reset/no_reset on COM4"
    RELEASE = "radar default_reset/hard_reset on COM4"

    def assert_released_once(self):
        # The OSSM is parked first, then the Radar is reset and monitored again.
        self.assertEqual(self.timeline.count(self.RELEASE), 1)
        self.assertEqual(self.timeline[self.timeline.index(self.RELEASE) - 1], "park ossm")
        self.assertEqual(self.timeline.index("monitor.start:radar"), self.timeline.index(self.RELEASE) + 1)

    BOOTS = ("device.probe", "boot to write hat", "observe")

    def assert_one_homing_per_guard_session(self):
        """Every OSSM boot that homes follows a park and a cleared guard session, and is parked after."""
        steps = [t for t in self.timeline if t in self.BOOTS + ("firmware.flash", "park ossm", "wait clear")]
        for i, step in enumerate(steps):
            if step in self.BOOTS:
                self.assertEqual(steps[i-2:i], ["park ossm", "wait clear"], (i, steps))
            if step in ("firmware.flash", "boot to write hat", "observe") and i + 1 < len(steps):
                self.assertEqual(steps[i+1], "park ossm", (i, steps))
        self.assertEqual(steps[-1], "park ossm")

    def test_radar_is_held_before_the_ossm_is_touched_and_released_after_the_restore(self):
        result = self.run_job("v2")
        self.assertEqual(result["status"], "success", result["summary"]["detail"])
        self.assertEqual(self.locks, ["ossm.lock", "radr.lock"])
        line = self.timeline
        # The bench stops monitoring the Radar, then it is parked before the OSSM probe, flash or any reset.
        self.assertLess(line.index("monitor.stop:radar"), line.index(self.HOLD))
        self.assertLess(line.index(self.HOLD), line.index("device.probe"))
        self.assertLess(line.index(self.HOLD), line.index("firmware.flash"))
        # Held through both cases and the restore; released, then monitored again.
        last_observe = len(line) - 1 - line[::-1].index("observe")
        last_park = len(line) - 1 - line[::-1].index("park ossm")
        self.assertEqual(self.observed[-1], "restore")
        self.assertLess(last_observe, last_park)
        self.assertLess(last_park, line.index(self.RELEASE))
        self.assert_released_once()
        self.assert_one_homing_per_guard_session()
        states = {c["title"]: c["state"] for c in result["summary"]["checks"]}
        self.assertEqual(states["Radar held in its bootloader so it cannot connect to the OSSM"], "passed")
        self.assertEqual(states["OSSM parked in its bootloader before the Radar was released"], "passed")
        self.assertEqual(states["Radar released into its application afterwards"], "passed")
        # The final park comes after the hat-auto restore and its observed boot.
        self.assertLess(line.index("hat auto", line.index("hat off")), last_park)

    def test_radar_is_released_on_every_failure_path(self):
        def case_fails(monitor):
            raise EvidenceError("OSSM lost its homed position during observation")
        def guard_unready(guard):
            raise RuntimeError("RS-485 safety guard: the guard has latched the motor off")
        for name, kwargs, status in (
                ("failed case", dict(observe=case_fails), "failed"),
                ("guard not ready", dict(guard_ready=guard_unready), "blocked"),
                ("flash refused", dict(flash_error=Blocked("Fixture has a different installed partition table")), "blocked"),
                ("bench lost", dict(flash_error=OSError("bench connection reset")), "blocked")):
            with self.subTest(name):
                self.setUp()
                result = self.run_job("v1", **kwargs)
                self.assertEqual(result["status"], status, result["summary"])
                self.assertIn(self.HOLD, self.timeline)
                self.assert_released_once()

    def test_a_held_radar_lock_blocks_before_any_hardware_step(self):
        result = self.run_job("v2", radar_lock_held=True)
        self.assertEqual(result["status"], "blocked")
        self.assertIn("radr.lock is held", result["summary"]["detail"])
        self.assertEqual(self.timeline, [])

    def test_an_unenrolled_radar_blocks_the_ossm(self):
        result = self.run_job("v2", radar=False)
        self.assertEqual(result["status"], "blocked")
        self.assertEqual(result["summary"]["detail"],
                         "The rig's Radar is not enrolled, so it cannot be held away from the OSSM")
        self.assertEqual((self.timeline, self.locks), ([], ["ossm.lock"]))

    def test_a_radar_that_cannot_be_held_blocks_and_is_still_released(self):
        for kwargs, reason in ((dict(radar_fails=("no_reset", RuntimeError("esptool exited with 2"))), "esptool exited"),
                               (dict(radar_mac="AABBCCDDEEFF"), "factory MAC")):
            with self.subTest(reason):
                self.setUp()
                result = self.run_job("v2", **kwargs)
                self.assertEqual(result["status"], "blocked")
                self.assertIn("could not be held in its bootloader", result["summary"]["detail"])
                self.assertIn(reason, result["summary"]["detail"])
                self.assertNotIn("device.probe", self.timeline)
                self.assertNotIn("firmware.flash", self.timeline)
                self.assert_released_once()

    def test_every_homing_boot_is_the_only_one_in_its_guard_session(self):
        result = self.run_job("v1")
        self.assertEqual(result["status"], "success", result["summary"]["detail"])
        self.assert_one_homing_per_guard_session()
        # Bench probe, three setting writes (two cases and the restore) and three observed boots.
        self.assertEqual([t for t in self.timeline if t in self.BOOTS],
                         ["device.probe"] + ["boot to write hat", "observe"] * 3)
        rows = {c["title"]: c for c in result["summary"]["checks"]}
        for case in ("With HAT", "No HAT"):
            row = rows[f"{case}: Homing was the only one in its guard session (two hard-stop contacts)"]
            self.assertEqual((row["state"], row["evidenceCount"]), ("passed", 2))

    def test_a_third_contact_is_reported_on_the_case(self):
        result = self.run_job("v2", contacts=("lower",))
        row = next(c for c in result["summary"]["checks"] if c["title"].startswith("With HAT: Homing was the only"))
        self.assertEqual((row["state"], row["reason"]), ("blocked", "1 contacts: lower"))

    def test_a_guard_session_that_never_clears_blocks_before_the_next_boot(self):
        # The second wait is the one before With HAT's setting-write boot.
        result = self.run_job("v2", clear_failures=(2,))
        self.assertEqual(result["status"], "blocked")
        self.assertIn("did not clear within 30 s", result["summary"]["detail"])
        self.assertNotIn("boot to write hat", self.timeline[:self.timeline.index("wait clear", self.timeline.index("firmware.flash"))])
        self.assert_one_homing_per_guard_session()
        self.assert_released_once()

    def test_a_session_that_never_clears_before_the_probe_blocks_the_job(self):
        result = self.run_job("v2", clear_failures=(1,))
        self.assertEqual(result["status"], "blocked")
        self.assertIn("did not clear within 30 s", result["summary"]["detail"])
        self.assertNotIn("device.probe", self.timeline)
        self.assertNotIn("firmware.flash", self.timeline)
        self.assert_released_once()

    def test_an_ossm_that_cannot_be_parked_keeps_the_radar_held(self):
        result = self.run_job("v2", park_error=RuntimeError("esptool exited with 2"))
        self.assertEqual(result["status"], "success")
        self.assertNotIn(self.RELEASE, self.timeline)
        self.assertNotIn("monitor.start:radar", self.timeline)
        self.assertIn("the OSSM could not be parked", result["summary"]["detail"])
        rows = {c["title"]: c for c in result["summary"]["checks"]}
        self.assertEqual(rows["OSSM parked in its bootloader before the Radar was released"]["state"], "blocked")
        self.assertIn("left in its bootloader", rows["Radar released into its application afterwards"]["reason"])

    def test_a_parked_ossm_is_not_handed_back_to_bench_monitoring(self):
        result = self.run_job("v2", ossm_monitored=True)
        self.assertEqual(result["status"], "success", result["summary"]["detail"])
        self.assertIn("monitor.stop", self.timeline)
        self.assertNotIn("monitor.start", self.timeline)  # the OSSM's; the Radar's is monitor.start:radar
        self.assertIn("monitor.start:radar", self.timeline)

    def test_a_radar_release_failure_is_reported_without_changing_the_result(self):
        result = self.run_job("v2", radar_fails=("hard_reset", RuntimeError("esptool exited with 2")))
        self.assertEqual(result["status"], "success")
        self.assertIn("The Radar was not released: reset into its application failed", result["summary"]["detail"])
        released = result["summary"]["checks"][-1]
        self.assertEqual((released["title"], released["state"]), ("Radar released into its application afterwards", "blocked"))
        # Bench monitoring still restarts, and the cleanup does not try a second time.
        self.assertEqual(self.timeline.count(self.RELEASE), 1)
        self.assertIn("monitor.start:radar", self.timeline)

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
        self.assertEqual(titles[1], "Radar held in its bootloader so it cannot connect to the OSSM")
        self.assertEqual(titles[-3:], ["HAT detection restored for the next run",
                                       "OSSM parked in its bootloader before the Radar was released",
                                       "Radar released into its application afterwards"])
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
        restored = next(c for c in result["summary"]["checks"] if c["title"] == "HAT detection restored for the next run")
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
