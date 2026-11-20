import json
import unittest

from monitor import EvidenceError, Monitor
from release_health import LockboxMonitor, PRODUCTION_MQTT, observations, observer_for

EXPECTED = dict(product="lockbox", device_id="TESTUNIT0001", variant="r2",build_sha="a"*40,image_sha256="b"*64,track="main")


def feed(monitor, now, event, **args):
    record = {"event":event,"uptime_ms":int(now*1000),"boot_id":93,**args}
    monitor.feed("LKBX_HEALTH "+json.dumps(record),now)


def start():
    monitor = LockboxMonitor(EXPECTED, 0)
    feed(monitor,1,"boot", **{k:v for k,v in EXPECTED.items() if k!="product"})
    feed(monitor,2,"wifi_connected",ip="192.168.1.20")
    feed(monitor,3,"mqtt_connected",broker=PRODUCTION_MQTT)
    return monitor


class ReleaseHealth(unittest.TestCase):
    def test_does_not_flash_identity_only_production_firmware(self):
        self.assertIsNone(observer_for(b'RAD_ID {"version":"%s"}',"ossm"))
        self.assertIsNone(observer_for(b'LKBX_HEALTH {"event":"heartbeat"',"lkbx"))
        self.assertIs(observer_for(b'RAD_HEALTH {"event":"boot" RAD_HEALTH {"event":"heartbeat"',"dtt"), Monitor)

    def test_verified_180s_of_both_clocks_and_application_progress(self):
        monitor = start()
        for now in range(5,186,5):
            feed(monitor,now,"heartbeat",main_menu=True,menu_age_ms=15)
        self.assertTrue(monitor.passed)
        rows = observations(monitor)
        self.assertTrue(all(c["state"] == "passed" for c in rows))
        self.assertEqual(rows[-1]["evidenceCount"],37)
        self.assertIn("180s device time",rows[-1]["reason"])

    def test_incomplete_boot_or_connectivity_evidence_never_passes(self):
        monitor = LockboxMonitor(EXPECTED,0)
        feed(monitor,1,"boot",**{k:v for k,v in EXPECTED.items() if k!="product"})
        for now in range(5,180,5):
            feed(monitor,now,"heartbeat",main_menu=True,menu_age_ms=1)
        self.assertFalse(monitor.passed)
        with self.assertRaises(EvidenceError):
            monitor.tick(181)
        with self.assertRaises(EvidenceError):
            feed(start(),4,"mqtt_connected",broker="staging")
        with self.assertRaises(EvidenceError):
            feed(start(),4,"wifi_connected",ip="0.0.0.0")

    def test_wrong_sha_reset_or_loss_of_liveness_fails(self):
        with self.assertRaises(EvidenceError):
            feed(LockboxMonitor(EXPECTED,0),1,"boot",**{**EXPECTED,"build_sha":"wrong"})
        for line in ("rst:0x1","panic","Task watchdog"):
            with self.assertRaises(EvidenceError):
                start().feed(line,4)
        monitor=start()
        feed(monitor,5,"heartbeat",main_menu=True,menu_age_ms=1)
        with self.assertRaises(EvidenceError):
            feed(monitor,10,"heartbeat",main_menu=True,menu_age_ms=20001)
        with self.assertRaises(EvidenceError):
            monitor.tick(31)

    def test_startup_and_continuous_network_health(self):
        monitor = start()
        monitor.tick(30)
        monitor.feed('LKBX_HEALTH {"event":"heartbeat","uptime_ms":[RAD BLE] event startup', 31)
        feed(monitor, 35, "heartbeat", main_menu=True, menu_age_ms=5)
        self.assertEqual(monitor.heartbeats, 1)
        for line in ("MQTT Disconnected", "Wi-Fi station disconnected (reason 8)",
                     'LKBX_HEALTH {"event":"heartbeat", [RAD BLE] event startup'):
            with self.assertRaises(EvidenceError):
                monitor.feed(line, 40)
        with self.assertRaises(EvidenceError):
            feed(monitor, 40, "mqtt_connected", broker=PRODUCTION_MQTT)


if __name__ == "__main__":
    unittest.main()
