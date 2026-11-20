"""Exercise admission, exact-variant intake and fail-closed physical results offline."""
import json
from pathlib import Path
from tempfile import TemporaryDirectory
import time
import unittest
from unittest.mock import patch
from uuid import uuid4

from release_admission import require_admission
from release_queue import Blocked, LeaseLost, Worker, assess_report, matching_board, verify_board


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

    def test_only_complete_verified_suite_can_pass(self):
        job = dict(deviceType="ossm", releaseId=str(uuid4()), buildSha=SHA, version="1.2.3")
        report = dict(id=str(uuid4()), suiteId="all", state="passed",family="ossm",deviceId="fixture",
                      coverage=dict(complete=True,required=["identity"],missing=[]),
                      firmware=dict(releaseId=job["releaseId"],buildSha=SHA,version=job["version"],verified=True),
                      cases=[{"state":"passed"}], cleanup={"attempted":True,"verified":True})
        self.assertEqual(assess_report(report,job,"fixture",DIGEST)[0],"success")
        for case in [dict(report,suiteId="smoke"), dict(report,deviceId="other"),
                     dict(report,firmware=dict(report["firmware"],buildSha="wrong")),
                     dict(report,cleanup={"attempted":True,"verified":False}),dict(report,cases=[])]:
            self.assertEqual(assess_report(case,job,"fixture",DIGEST)[0],"blocked")
        blocked = dict(report,state="blocked",cases=[dict(state="blocked",title="Measurement",reason="No sensor")])
        self.assertIn("No sensor",assess_report(blocked,job,"fixture",DIGEST)[1]["detail"])


if __name__ == "__main__":
    unittest.main()
