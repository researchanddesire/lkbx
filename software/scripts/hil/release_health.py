"""Physical release evidence from telemetry present in the immutable application."""
from hashlib import sha256
import ipaddress
import json

from monitor import EvidenceError, FAULT, Monitor

PRODUCTION_MQTT = "mqtts://te9acc16.ala.us-east-1.emqxsl.com"


def observer_for(application, family):
    if family == "lkbx" and all(b'LKBX_HEALTH {"event":"' + event in application for event in
                                (b'boot"', b'wifi_connected"', b'mqtt_connected"', b'heartbeat"')):
        return LockboxMonitor
    if all(b'RAD_HEALTH {"event":"' + event in application for event in (b'boot"', b'heartbeat"')):
        return Monitor
    return None


def runtime_digest(application):
    if len(application) < 56 or application[0] != 0xe9:
        raise ValueError("Registered application is not an ESP image")
    if application[23] != 1:
        return sha256(application).hexdigest()
    actual = sha256(application[:-32]).digest()
    if actual != application[-32:]:
        raise ValueError("Registered ESP runtime digest is invalid")
    return actual.hex()


class LockboxMonitor:
    """The Lockbox's ordinary production health protocol, including its production broker."""
    def __init__(self, expected, started):
        self.expected = expected
        self.started = started
        self.boot_id = None
        self.markers = set()
        self.wifi = self.mqtt = self.passed = False
        self.last_heartbeat = self.last_uptime = self.soak_started = self.soak_uptime = None
        self.heartbeats = 0

    def tick(self, now):
        if self.soak_started is None and now - self.started > Monitor.STARTUP_SECONDS:
            raise EvidenceError("Wi-Fi, production MQTT and menu readiness did not arrive within 180 seconds")
        if self.last_heartbeat is not None and now - self.last_heartbeat > Monitor.HEARTBEAT_SECONDS:
            raise EvidenceError("Menu heartbeat stopped for over 20 seconds")

    def feed(self, line, now):
        self.tick(now)
        lower = line.lower()
        if "wi-fi station disconnected" in lower or "mqtt disconnected" in lower:
            if self.soak_started is not None:
                raise EvidenceError("Lockbox lost its Wi-Fi or MQTT connection during observation")
            self.mqtt = False
            if "wi-fi station disconnected" in lower:
                self.wifi = False
        if FAULT.search(line):
            raise EvidenceError("Lockbox reported a reset, panic, watchdog or brownout")
        for marker in ("rst:0x", "esp-rom:"):
            if marker in lower:
                if self.boot_id is not None or marker in self.markers:
                    raise EvidenceError("Lockbox unexpectedly rebooted")
                self.markers.add(marker)
        if "LKBX_HEALTH " not in line:
            return
        try:
            record = json.loads(line.split("LKBX_HEALTH ", 1)[1])
        except ValueError as error:
            # Arduino and the IDF console can interleave startup messages. A
            # damaged startup heartbeat cannot start or extend any deadline.
            if (self.boot_id is not None and self.soak_started is None
                    and 'LKBX_HEALTH {"event":"heartbeat"' in line and "[RAD BLE] event " in line):
                return
            raise EvidenceError("Malformed Lockbox health telemetry") from error
        if not isinstance(record, dict):
            raise EvidenceError("Invalid Lockbox health telemetry")
        boot, uptime, event = record.get("boot_id"), record.get("uptime_ms"), record.get("event")
        if type(boot) is not int or not 0 <= boot <= 0xffffffff or type(uptime) is not int or uptime < 0:
            raise EvidenceError("Invalid boot identity or device uptime")
        if event == "boot":
            if self.boot_id is not None or any(record.get(k) != self.expected[k] for k in
                                               ("device_id", "build_sha", "image_sha256", "track", "variant")):
                raise EvidenceError("Lockbox boot, hardware variant or running image differs from the candidate")
            self.boot_id = boot
            self.last_uptime = uptime
            return
        if self.boot_id is None or self.boot_id != boot:
            raise EvidenceError("Lockbox did not produce fresh, continuous boot evidence")
        if self.soak_started is not None and event in ("wifi_connected", "mqtt_connected"):
            raise EvidenceError("Lockbox reconnected during the observation")
        if event == "wifi_connected":
            try:
                ip = ipaddress.IPv4Address(record.get("ip", ""))
            except ValueError as error:
                raise EvidenceError("Lockbox did not obtain IPv4") from error
            if ip.is_unspecified or ip.is_loopback or ip.is_link_local or ip.is_multicast or str(ip) == "255.255.255.255":
                raise EvidenceError("Lockbox did not obtain a usable IPv4 address")
            self.wifi = True
        elif event == "mqtt_connected":
            if record.get("broker") != PRODUCTION_MQTT:
                raise EvidenceError("Lockbox connected to an unexpected MQTT broker")
            self.mqtt = True
        elif event == "heartbeat":
            if uptime <= self.last_uptime:
                raise EvidenceError("Lockbox device uptime stopped advancing")
            self.last_uptime = uptime
            self.last_heartbeat = now
            self.heartbeats += 1
            age = record.get("menu_age_ms")
            ready = record.get("main_menu") is True and type(age) is int and 0 <= age <= 20000
            if self.soak_started is not None and not ready:
                raise EvidenceError("Lockbox menu task stopped making progress")
            if self.soak_started is None and self.wifi and self.mqtt and ready:
                self.soak_started, self.soak_uptime = now, uptime
            if self.soak_started is not None:
                self.passed = now - self.soak_started >= Monitor.SOAK_SECONDS and uptime - self.soak_uptime >= Monitor.SOAK_SECONDS * 1000
        else:
            raise EvidenceError("Unknown Lockbox health event")


def observations(observer, outcome="success", detail=""):
    def check(title, count):
        return {"title": title, "state": "passed" if count else "blocked", "evidenceCount": count}
    extra = ([check("Usable IPv4 address", int(observer.wifi)),
              check("Authenticated production MQTT connection", int(observer.mqtt))]
             if isinstance(observer, LockboxMonitor) else [])
    seconds = 0 if observer.soak_started is None else int(observer.last_heartbeat - observer.soak_started)
    elapsed = 0 if observer.soak_uptime is None else int((observer.last_uptime - observer.soak_uptime) / 1000)
    soak = {"title": "Continuous application and network observation",
            "state": "passed" if observer.passed else "blocked", "evidenceCount": observer.heartbeats,
            "reason": f"{seconds}s host time, {elapsed}s device time"}
    identity = check("Running image, board, build SHA and track", int(observer.boot_id is not None))
    if outcome == "failed":
        failed_check = identity if observer.boot_id is None else soak
        failed_check["state"] = "failed"
        failed_check["reason"] = detail[:400]
    return [check("Physical application SHA-256 readback", 1), identity, *extra, soak]
