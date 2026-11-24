import json
import unittest

from monitor import EvidenceError, HAT_PREFIX, Monitor
from release_health import (DAMAGED_LIMIT, LockboxMonitor, NO_HAT, OssmMonitor, PRODUCTION_MQTT, ReadinessNotReached,
                            RadrMonitor, StartupTimeout, TrainerMonitor, WITH_HAT, case_checks, observations,
                            observer_for, skipped_checks)

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
        # A damaged record during the soak is ignored and counted, not fatal.
        monitor.feed('LKBX_HEALTH {"event":"heartbeat", [RAD BLE] event startup', 38)
        self.assertEqual((monitor.damaged, monitor.heartbeats), (2, 1))
        for line in ("MQTT Disconnected", "Wi-Fi station disconnected (reason 8)"):
            with self.assertRaises(EvidenceError):
                monitor.feed(line, 40)
        with self.assertRaises(EvidenceError):
            feed(monitor, 40, "mqtt_connected", broker=PRODUCTION_MQTT)

    def test_lockbox_startup_message_is_unchanged(self):
        with self.assertRaises(StartupTimeout) as raised:
            LockboxMonitor(EXPECTED, 0).tick(181)
        self.assertEqual(str(raised.exception), "Wi-Fi, production MQTT and menu readiness did not arrive within 180 seconds")


def image(prefix, *events):
    return b" ".join((prefix + '{"event":"' + event + '"').encode() for event in events)


IDENTITY = {k: v for k, v in EXPECTED.items() if k != "product"}


def emit(monitor, prefix, now, event, **args):
    monitor.feed(prefix + json.dumps({"event": event, "uptime_ms": int(now * 1000), "boot_id": 7, **args}), now)


def hat_status(**flags):
    status = dict(detected=True, fresh=True, passive=True, guardReady=True, guardArmed=False,
                  guardLatched=False, outputDisabled=True, guardFault=0)
    return HAT_PREFIX + json.dumps({**status, **flags})


class ObserverSelection(unittest.TestCase):
    def test_production_prefix_is_checked_before_rad_health(self):
        staging = image("RAD_HEALTH ", "boot", "heartbeat")
        for family, monitor in (("ossm", OssmMonitor), ("dtt", TrainerMonitor), ("radr", RadrMonitor)):
            production = image(monitor.PREFIX, "boot", "heartbeat")
            self.assertIs(observer_for(production, family), monitor)
            self.assertIs(observer_for(production + b" " + staging, family), monitor)
            self.assertIs(observer_for(image(monitor.PREFIX, "boot") + b" " + staging, family), Monitor)
            self.assertIsNone(observer_for(image(monitor.PREFIX, "heartbeat"), family))
            # Another product's prefix never qualifies an image.
            self.assertIsNone(observer_for(image("LKBX_HEALTH ", "boot", "wifi_connected", "mqtt_connected", "heartbeat"), family))
        self.assertIsNone(observer_for(image("OSSM_HEALTH ", "boot", "heartbeat"), "lkbx"))
        self.assertIs(observer_for(image("LKBX_HEALTH ", "boot", "wifi_connected", "mqtt_connected", "heartbeat"), "lkbx"),
                      LockboxMonitor)


class TrainerAndRadr(unittest.TestCase):
    def soak(self, monitor, start=5, stop=186):
        for now in range(start, stop, 5):
            emit(monitor, monitor.PREFIX, now, "heartbeat", main_menu=True, menu_age_ms=3, ram_free=9000, ram_largest=4000)

    def test_trainer_needs_wifi_and_its_cloud_health_check(self):
        monitor = TrainerMonitor(dict(EXPECTED, variant="v1"), 0)
        emit(monitor, "DTT_HEALTH ", 1, "boot", **dict(IDENTITY, variant="v1"), reset_reason=1)
        emit(monitor, "DTT_HEALTH ", 2, "wifi_connected", ip="192.168.1.9")
        emit(monitor, "DTT_HEALTH ", 4, "heartbeat", main_menu=True, menu_age_ms=3)
        self.assertIsNone(monitor.soak_started)
        emit(monitor, "DTT_HEALTH ", 5, "cloud_ok")
        self.soak(monitor, 10, 191)
        self.assertTrue(monitor.passed)
        titles = [row["title"] for row in observations(monitor)]
        self.assertIn("Successful production cloud health check", titles)
        self.assertNotIn("Authenticated production MQTT connection", titles)
        self.assertTrue(all(row["state"] == "passed" for row in observations(monitor)))

    def test_trainer_cloud_recovery_during_the_soak_fails(self):
        monitor = TrainerMonitor(dict(EXPECTED, variant="v2"), 0)
        emit(monitor, "DTT_HEALTH ", 1, "boot", **dict(IDENTITY, variant="v2"))
        emit(monitor, "DTT_HEALTH ", 2, "wifi_connected", ip="192.168.1.9")
        emit(monitor, "DTT_HEALTH ", 3, "cloud_ok")
        self.soak(monitor, 5, 50)
        with self.assertRaisesRegex(EvidenceError, "reconnected"):
            emit(monitor, "DTT_HEALTH ", 51, "cloud_ok")
        other = TrainerMonitor(EXPECTED, 0)
        emit(other, "DTT_HEALTH ", 1, "boot", **IDENTITY)
        with self.assertRaisesRegex(EvidenceError, "Unknown DTT"):
            emit(other, "DTT_HEALTH ", 2, "mqtt_connected", broker=PRODUCTION_MQTT)

    def test_radr_needs_wifi_only(self):
        monitor = RadrMonitor(dict(EXPECTED, variant="r8"), 0)
        emit(monitor, "RADR_HEALTH ", 1, "boot", **dict(IDENTITY, variant="r8"))
        with self.assertRaisesRegex(EvidenceError, "usable IPv4"):
            emit(monitor, "RADR_HEALTH ", 2, "wifi_connected", ip="169.254.1.1")
        emit(monitor, "RADR_HEALTH ", 2, "wifi_connected", ip="192.168.1.9")
        self.soak(monitor)
        self.assertTrue(monitor.passed)
        self.assertEqual([row["title"] for row in observations(monitor)][2], "Usable IPv4 address")
        self.assertEqual(len(observations(monitor)), 4)
        with self.assertRaises(StartupTimeout):
            RadrMonitor(EXPECTED, 0).tick(181)

    def test_other_products_never_parse_lockbox_lines(self):
        monitor = RadrMonitor(dict(EXPECTED, variant="r8"), 0)
        emit(monitor, "LKBX_HEALTH ", 1, "boot", **IDENTITY)
        self.assertIsNone(monitor.boot_id)


class OssmCases(unittest.TestCase):
    def start(self, case, mode=None):
        monitor = OssmMonitor(dict(EXPECTED, variant="4mb"), 0, case)
        emit(monitor, "OSSM_HEALTH ", 1, "boot", **dict(IDENTITY, variant="4mb"), hat_mode=mode or case.mode)
        emit(monitor, "OSSM_HEALTH ", 2, "wifi_connected", ip="192.168.1.30")
        emit(monitor, "OSSM_HEALTH ", 3, "mqtt_connected", broker=PRODUCTION_MQTT)
        return monitor

    def beat(self, monitor, now, hat, homed=True, **args):
        emit(monitor, "OSSM_HEALTH ", now, "heartbeat", main_menu=True, menu_age_ms=4, hat=hat, homed=homed, **args)

    def test_with_hat_waits_for_detection_and_homing(self):
        monitor = self.start(WITH_HAT)
        self.beat(monitor, 5, False, False)
        self.beat(monitor, 10, False, True)
        self.beat(monitor, 15, True, False)
        self.assertIsNone(monitor.soak_started)
        monitor.feed(hat_status(detected=False, passive=False), 16)  # still discovering
        for now in range(20, 201, 5):
            self.beat(monitor, now, True)
            monitor.feed(hat_status(), now + 0.5)
        self.assertTrue(monitor.passed)
        rows = case_checks(monitor, prefix="With HAT: ")
        self.assertTrue(all(row["title"].startswith("With HAT: ") for row in rows))
        self.assertTrue(all(row["state"] == "passed" for row in rows), rows)
        self.assertIn("With HAT: HAT mode auto at boot", [row["title"] for row in rows])

    def test_no_hat_needs_detection_off_and_homing(self):
        monitor = self.start(NO_HAT)
        self.beat(monitor, 5, False, False)
        for now in range(10, 191, 5):
            self.beat(monitor, now, False)
        self.assertTrue(monitor.passed)
        self.assertIn("No HAT: HAT detection off, carriage homed on the board's current sensor",
                      [row["title"] for row in case_checks(monitor, prefix="No HAT: ")])
        for line in (hat_status(), hat_status(passive=False)):
            with self.assertRaisesRegex(EvidenceError, "detection was off"):
                self.start(NO_HAT).feed(line, 4)
        with self.assertRaisesRegex(EvidenceError, "detection was off"):
            self.beat(self.start(NO_HAT), 5, True, False)

    def test_wrong_or_missing_boot_hat_mode_fails(self):
        with self.assertRaisesRegex(EvidenceError, "booted with HAT mode auto, not off"):
            self.start(NO_HAT, mode="auto")
        with self.assertRaisesRegex(EvidenceError, "HAT mode"):
            emit(OssmMonitor(EXPECTED, 0, WITH_HAT), "OSSM_HEALTH ", 1, "boot", **IDENTITY)
        with self.assertRaisesRegex(EvidenceError, "booted with HAT mode off"):
            self.start(WITH_HAT, mode="off")

    def soaking(self, case):
        monitor = self.start(case)
        for now in range(5, 30, 5):
            self.beat(monitor, now, case.hat)
        self.assertIsNotNone(monitor.soak_started)
        return monitor

    def test_losses_during_the_soak_fail(self):
        with self.assertRaisesRegex(EvidenceError, "homed"):
            self.beat(self.soaking(WITH_HAT), 30, True, False)
        with self.assertRaisesRegex(EvidenceError, "lost its HAT"):
            self.beat(self.soaking(WITH_HAT), 30, False)
        with self.assertRaisesRegex(EvidenceError, "driving the RS-485 bus"):
            self.soaking(WITH_HAT).feed(hat_status(passive=False), 30)
        with self.assertRaisesRegex(EvidenceError, "safety latch"):
            self.soaking(NO_HAT).feed(hat_status(detected=False, guardLatched=True, guardFault=16), 30)
        with self.assertRaisesRegex(EvidenceError, "MQTT connection"):
            self.soaking(WITH_HAT).feed("E (40) MQTT_CLIENT: Error transport connect", 30)
        with self.assertRaisesRegex(EvidenceError, "reconnected"):
            emit(self.soaking(NO_HAT), "OSSM_HEALTH ", 30, "mqtt_connected", broker=PRODUCTION_MQTT)
        with self.assertRaisesRegex(EvidenceError, "heartbeat did not report"):
            emit(self.soaking(NO_HAT), "OSSM_HEALTH ", 30, "heartbeat", main_menu=True, menu_age_ms=1)

    def test_mqtt_drop_before_the_soak_needs_a_new_connection(self):
        monitor = self.start(WITH_HAT)
        monitor.feed("E (40) MQTT_CLIENT: Error transport connect", 4)
        self.beat(monitor, 5, True)
        self.assertIsNone(monitor.soak_started)
        emit(monitor, "OSSM_HEALTH ", 6, "mqtt_connected", broker=PRODUCTION_MQTT)
        self.beat(monitor, 10, True)
        self.assertIsNotNone(monitor.soak_started)
        with self.assertRaisesRegex(EvidenceError, "unexpected MQTT broker"):
            emit(self.start(WITH_HAT), "OSSM_HEALTH ", 4, "mqtt_connected", broker="mqtts://x15ff600.ala.us-east-1.emqxsl.com")

    def test_a_hat_poll_reply_inside_a_heartbeat_is_dropped(self):
        monitor = self.soaking(WITH_HAT)
        monitor.feed('OSSM_HEALTH {"event":"heartbeat","boot_id":7,' + hat_status() + ',"uptime_ms":31000}', 31)
        self.beat(monitor, 35, True)
        monitor.feed('OSSM_HEALTH {"event":"wifi_connected",' + hat_status(), 36)
        self.assertEqual(monitor.damaged, 2)
        # Dropped records never stand in for heartbeats.
        with self.assertRaisesRegex(EvidenceError, "stopped for over 20 seconds"):
            self.soaking(WITH_HAT).tick(46)

    def test_skipped_case_lists_blocked_checks(self):
        rows = skipped_checks(NO_HAT, "Not run: With HAT could not run")
        self.assertTrue(rows and all(row["state"] == "blocked" and row["title"].startswith("No HAT: ") for row in rows))
        self.assertTrue(all(row["reason"] == "Not run: With HAT could not run" for row in rows))

    def test_failed_case_names_the_failing_check(self):
        monitor = self.start(NO_HAT)
        rows = case_checks(monitor, "failed", "OSSM lost its homed position during observation", "No HAT: ", annotate=True)
        self.assertEqual(rows[-1]["state"], "failed")
        self.assertIn("homed", rows[-1]["reason"])
        rows = case_checks(OssmMonitor(EXPECTED, 0, NO_HAT), "blocked", "No boot evidence", "No HAT: ", annotate=True)
        self.assertEqual((rows[0]["state"], rows[0]["reason"]), ("blocked", "No boot evidence"))


class DamagedRecords(unittest.TestCase):
    """The first line after a reset is often the previous boot's record cut in half."""
    def lockbox_run(self, before=(), during=()):
        monitor = LockboxMonitor(EXPECTED, 0)
        for line in before:
            monitor.feed(line, 0.5)
        feed(monitor, 1, "boot", **IDENTITY)
        feed(monitor, 2, "wifi_connected", ip="192.168.1.20")
        feed(monitor, 3, "mqtt_connected", broker=PRODUCTION_MQTT)
        for now in range(5, 186, 5):
            feed(monitor, now, "heartbeat", main_menu=True, menu_age_ms=15)
            if now in dict(during):
                monitor.feed(dict(during)[now], now + 1)
        return monitor

    def test_reset_tail_and_previous_boot_records_before_boot_are_ignored(self):
        monitor = self.lockbox_run(before=(
            'LKBX_HEALTH {"event":"heartbeat","boot_id":12,"upt',
            'ime_ms":5000}ESP-ROM:esp32s3-20210327 LKBX_HEALTH {"event":"hea',
            'LKBX_HEALTH {"event":"heartbeat","boot_id":12,"uptime_ms":99000,"main_menu":true,"menu_age_ms":3}',
            'LKBX_HEALTH [1, 2]'))
        self.assertTrue(monitor.passed)
        self.assertEqual((monitor.before_boot, monitor.damaged), (4, 0))
        rows = observations(monitor)
        self.assertTrue(all(row["state"] == "passed" for row in rows))
        self.assertEqual(rows[-1]["reason"], "180s host time, 180s device time; 4 records before boot ignored")

    def test_damaged_records_mid_soak_are_ignored_up_to_the_limit(self):
        damaged = 'LKBX_HEALTH {"event":"heartbeat","boot_id":93,"uptime_ms":4'
        monitor = self.lockbox_run(during=[(now, damaged) for now in range(20, 20 + 5 * DAMAGED_LIMIT, 5)])
        self.assertTrue(monitor.passed)
        self.assertEqual(monitor.damaged, DAMAGED_LIMIT)
        self.assertIn(f"{DAMAGED_LIMIT} damaged health records ignored", observations(monitor)[-1]["reason"])
        with self.assertRaisesRegex(EvidenceError, "Repeated damaged Lockbox health telemetry"):
            self.lockbox_run(during=[(now, damaged) for now in range(20, 25 + 5 * DAMAGED_LIMIT, 5)])

    def test_every_product_ignores_then_limits_damaged_records(self):
        for monitor_type, args in ((OssmMonitor, (WITH_HAT,)), (TrainerMonitor, ()), (RadrMonitor, ())):
            monitor = monitor_type(EXPECTED, 0, *args)
            monitor.feed(monitor_type.PREFIX + '{"event":"boot","boot_id":', 0.5)
            emit(monitor, monitor_type.PREFIX, 1, "boot", **IDENTITY, hat_mode="auto")
            self.assertEqual(monitor.boot_id, 7)
            for n in range(DAMAGED_LIMIT):
                monitor.feed(monitor_type.PREFIX + '{"event":"heart', 2 + n / 10)
            with self.assertRaisesRegex(EvidenceError, "Repeated damaged"):
                monitor.feed(monitor_type.PREFIX + "}", 3)

    def test_records_of_another_boot_after_this_boot_still_fail(self):
        monitor = self.lockbox_run()
        with self.assertRaisesRegex(EvidenceError, "fresh, continuous boot evidence"):
            monitor.feed('LKBX_HEALTH {"event":"heartbeat","boot_id":12,"uptime_ms":190000,"main_menu":true,"menu_age_ms":3}', 186)

    def test_a_missing_event_behind_damage_is_named_with_the_damage(self):
        monitor = LockboxMonitor(EXPECTED, 0)
        feed(monitor, 1, "boot", **IDENTITY)
        feed(monitor, 2, "wifi_connected", ip="192.168.1.20")
        monitor.feed('LKBX_HEALTH {"event":"mqtt_connected","boot_id":93,"uptime_ms":3000,"bro', 3)
        for now in range(5, 180, 5):
            feed(monitor, now, "heartbeat", main_menu=True, menu_age_ms=15)
        with self.assertRaises(ReadinessNotReached) as raised:
            monitor.tick(181)
        self.assertEqual(str(raised.exception), "Readiness not reached in 180 s: production MQTT never connected; "
                                                "1 damaged health records were ignored (heartbeats continued)")


class Readiness(unittest.TestCase):
    """A healthy board that never becomes ready is blocked by its environment, not failed."""
    def run_until_deadline(self, monitor, events=(), menu=True, product=None, stop=180, start=5):
        prefix = monitor.PREFIX
        emit(monitor, prefix, 1, "boot", **IDENTITY, hat_mode=getattr(monitor, "case", WITH_HAT).mode)
        for now, event, args in events:
            emit(monitor, prefix, now, event, **args)
        for now in range(start, stop, 5):
            emit(monitor, prefix, now, "heartbeat", main_menu=menu, menu_age_ms=10 if menu else now * 1000,
                 **(product or {}))
        monitor.tick(179.5)  # nothing is raised before the deadline
        with self.assertRaises(ReadinessNotReached) as raised:
            monitor.tick(181)
        return str(raised.exception)

    WIFI = (2, "wifi_connected", {"ip": "192.168.1.20"})
    MQTT = (3, "mqtt_connected", {"broker": PRODUCTION_MQTT})

    def test_lockbox_without_production_sign_in(self):
        # The rig's Lockboxes hold staging device keys: Wi-Fi only, menu never reached.
        detail = self.run_until_deadline(LockboxMonitor(EXPECTED, 0), [self.WIFI], menu=False)
        self.assertEqual(detail, "Readiness not reached in 180 s: production MQTT never connected; "
                                 "main menu never reached (heartbeats continued)")

    def test_every_product_names_what_is_missing(self):
        cases = [
            (LockboxMonitor(EXPECTED, 0), [], True, None, "Wi-Fi never connected; production MQTT never connected"),
            (TrainerMonitor(EXPECTED, 0), [self.WIFI], True, None, "production cloud health check never succeeded"),
            (RadrMonitor(EXPECTED, 0), [], False, None, "Wi-Fi never connected; main menu never reached"),
            (OssmMonitor(EXPECTED, 0, WITH_HAT), [self.WIFI, self.MQTT], True, {"hat": False, "homed": True},
             "HAT never detected"),
            (OssmMonitor(EXPECTED, 0, NO_HAT), [self.WIFI], True, {"hat": False, "homed": False},
             "production MQTT never connected; carriage never homed"),
        ]
        for monitor, events, menu, product, missing in cases:
            with self.subTest(monitor=type(monitor).__name__, missing=missing):
                detail = self.run_until_deadline(monitor, events, menu, product)
                self.assertEqual(detail, f"Readiness not reached in 180 s: {missing} (heartbeats continued)")

    def test_a_connection_lost_before_the_soak_is_named(self):
        monitor = LockboxMonitor(EXPECTED, 0)
        feed(monitor, 1, "boot", **IDENTITY)
        feed(monitor, 2, "wifi_connected", ip="192.168.1.20")
        feed(monitor, 3, "mqtt_connected", broker=PRODUCTION_MQTT)
        monitor.feed("MQTT Disconnected", 4)
        for now in range(5, 180, 5):
            feed(monitor, now, "heartbeat", main_menu=now < 100, menu_age_ms=10)
        with self.assertRaisesRegex(ReadinessNotReached, "production MQTT dropped and did not reconnect; "
                                                         "main menu not reached at the deadline"):
            monitor.tick(181)

    def test_device_faults_still_fail(self):
        # No heartbeat at all after the boot record.
        monitor = LockboxMonitor(EXPECTED, 0)
        feed(monitor, 1, "boot", **IDENTITY)
        with self.assertRaisesRegex(EvidenceError, "no heartbeat") as raised:
            monitor.tick(181)
        self.assertNotIsInstance(raised.exception, StartupTimeout)
        # Heartbeats that stopped: the gap rule wins over the deadline.
        monitor = LockboxMonitor(EXPECTED, 0)
        feed(monitor, 1, "boot", **IDENTITY)
        for now in range(5, 160, 5):
            feed(monitor, now, "heartbeat", main_menu=False, menu_age_ms=10)
        with self.assertRaisesRegex(EvidenceError, "stopped for over 20 seconds") as raised:
            monitor.tick(181)
        self.assertNotIsInstance(raised.exception, StartupTimeout)
        # Faults raised on their own line are never readiness.
        for line in ("rst:0x1 (POWERON_RESET)", "Guru Meditation Error", "Brownout detector was triggered"):
            monitor = LockboxMonitor(EXPECTED, 0)
            monitor.feed("rst:0x1 (POWERON_RESET)", 0.5)
            feed(monitor, 1, "boot", **IDENTITY)
            with self.assertRaises(EvidenceError) as raised:
                monitor.feed(line, 50)
            self.assertNotIsInstance(raised.exception, ReadinessNotReached)
        monitor = LockboxMonitor(EXPECTED, 0)
        feed(monitor, 1, "boot", **IDENTITY)
        feed(monitor, 5, "heartbeat", main_menu=False, menu_age_ms=10)
        with self.assertRaisesRegex(EvidenceError, "uptime stopped advancing"):
            monitor.feed('LKBX_HEALTH {"event":"heartbeat","boot_id":93,"uptime_ms":5000,"main_menu":false,"menu_age_ms":1}', 10)

    def test_no_boot_record_is_still_plain_startup_timeout(self):
        with self.assertRaises(StartupTimeout) as raised:
            OssmMonitor(EXPECTED, 0, NO_HAT).tick(181)
        self.assertNotIsInstance(raised.exception, ReadinessNotReached)


if __name__ == "__main__":
    unittest.main()
