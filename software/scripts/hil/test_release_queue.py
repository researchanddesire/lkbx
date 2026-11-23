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

from release_admission import require_admission
from release_health import PRODUCTION_MQTT
from release_queue import Blocked, LeaseLost, Worker, default_runner, matching_board, verify_board


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


class RunnerNameTests(unittest.TestCase):
    def test_configured_name_wins(self):
        self.assertEqual(default_runner({"FIRMWARE_VALIDATION_RIG_NAME": " bench-a ", "COMPUTERNAME": "HOST"}), "bench-a")

    def test_computer_name_is_lowercased_with_the_runner_suffix(self):
        self.assertEqual(default_runner({"COMPUTERNAME": "BENCH-HOST"}), "bench-host-lkbx-rig-01")

    def test_unknown_host_has_no_name(self):
        self.assertEqual(default_runner({}), "")


if __name__ == "__main__":
    unittest.main()
