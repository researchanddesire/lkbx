"""Physical release evidence from telemetry present in the immutable application."""
from collections import namedtuple
from hashlib import sha256
import ipaddress
import json

from monitor import EvidenceError, FAULT, Monitor, STOP_PATH_UNVERIFIED, parse_hat_status

PRODUCTION_MQTT = "mqtts://te9acc16.ala.us-east-1.emqxsl.com"
NETWORK_EVENTS = ("wifi_connected", "mqtt_connected", "cloud_ok")
NETWORK_TITLES = {"wifi_connected": "Usable IPv4 address",
                  "mqtt_connected": "Authenticated production MQTT connection",
                  "cloud_ok": "Successful production cloud health check"}
IDENTITY = ("device_id", "build_sha", "image_sha256", "track", "variant")

# Every OSSM release runs both cases on the enrolled board. "With HAT" is the
# guarded bench: the PC guard polls the servo, so the HAT only listens and
# motion stays on STEP/DIR. "No HAT" stores the serial `hat off` setting, so
# the OSSM never drives the RS-485 bus and homes on its own current sensor.
HatCase = namedtuple("HatCase", "name mode hat")
WITH_HAT = HatCase("With HAT", "auto", True)
NO_HAT = HatCase("No HAT", "off", False)
HAT_CASES = (WITH_HAT, NO_HAT)


# Damaged records after the boot record that one observation may ignore.
DAMAGED_LIMIT = 5


class StartupTimeout(EvidenceError):
    """Readiness did not arrive in time. Without a boot record the board never ran."""


class ReadinessNotReached(StartupTimeout):
    """A healthy, running board never reached readiness: an environment problem, not a fault.

    The boot record matched and heartbeats kept arriving with advancing uptime,
    but a required connection or the main menu never arrived (for example a
    rig Lockbox whose device key production does not accept).
    """


# What each missing piece of readiness is called in the evidence.
MISSING_EVENTS = {"wifi_connected": ("Wi-Fi never connected", "Wi-Fi dropped and did not reconnect"),
                  "mqtt_connected": ("production MQTT never connected", "production MQTT dropped and did not reconnect"),
                  "cloud_ok": ("production cloud health check never succeeded",
                               "production cloud health check never succeeded")}


class ProductionMonitor:
    """An ordinary production health line, `<PREFIX>_HEALTH {json}`, always compiled in.

    Each product names the network events it must report before the soak and
    the console text that means one of those connections dropped.
    """
    PREFIX = NAME = STARTUP = None
    EVENTS = ("boot", "wifi_connected", "heartbeat")
    REQUIRED = ("wifi_connected",)
    # Present in the image before this observer is chosen over RAD_HEALTH.
    IMAGE_EVENTS = ("boot", "heartbeat")
    LOSSES = ()

    def __init__(self, expected, started):
        self.expected = expected
        self.started = started
        self.boot_id = None
        self.markers = set()
        self.connected = set()
        self.passed = False
        self.last_heartbeat = self.last_uptime = self.soak_started = self.soak_uptime = None
        self.heartbeats = 0
        # Records ignored before the boot record (the previous boot's output and
        # the reset tail), and damaged records ignored after it.
        self.before_boot = 0
        self.damaged = 0
        # Ever seen this boot, for naming what readiness lacked.
        self.seen = set()
        self.menu_seen = self.menu_last = False

    @property
    def wifi(self):
        return "wifi_connected" in self.connected

    @property
    def mqtt(self):
        return "mqtt_connected" in self.connected

    def tick(self, now):
        if self.last_heartbeat is not None and now - self.last_heartbeat > Monitor.HEARTBEAT_SECONDS:
            raise EvidenceError("Menu heartbeat stopped for over 20 seconds")
        if self.soak_started is None and now - self.started > Monitor.STARTUP_SECONDS:
            if self.boot_id is None:
                raise StartupTimeout(self.STARTUP)
            if not self.heartbeats:
                raise EvidenceError(f"{self.NAME} sent no heartbeat within 180 seconds of its boot record")
            # Boot identity matched, and heartbeats kept arriving within the gap
            # rule with advancing uptime and no reset or fault (each of those
            # fails on its own line), so the board ran; readiness did not.
            missing = self.missing()
            if self.damaged:
                missing.append(f"{self.damaged} damaged health records were ignored")
            raise ReadinessNotReached(f"Readiness not reached in {Monitor.STARTUP_SECONDS} s: "
                                      + "; ".join(missing or ["readiness came too late to start the soak"])
                                      + " (heartbeats continued)")

    def missing(self):
        """Each piece of readiness this boot lacked at the deadline."""
        phrases = [MISSING_EVENTS[event][event in self.seen] for event in self.REQUIRED
                   if event not in self.connected]
        if not self.menu_seen:
            phrases.append("main menu never reached")
        elif not self.menu_last:
            phrases.append("main menu not reached at the deadline")
        return phrases + self.product_missing()

    def product_missing(self):
        return []

    def console(self, line):
        """Product checks on other console records after boot."""

    def booted(self, record):
        """Product checks on the boot record after its identity matched."""

    def ready(self, record, menu_ready):
        """Product readiness on a heartbeat; may fail the run during the soak."""
        return menu_ready

    def feed(self, line, now):
        self.tick(now)
        lower = line.lower()
        lost = {event for text, events in self.LOSSES if text in lower for event in events}
        if lost:
            if self.soak_started is not None:
                raise EvidenceError(f"{self.NAME} lost its Wi-Fi or MQTT connection during observation")
            self.connected -= lost
        if FAULT.search(line):
            raise EvidenceError(f"{self.NAME} reported a reset, panic, watchdog or brownout")
        for marker in ("rst:0x", "esp-rom:"):
            if marker in lower:
                if self.boot_id is not None or marker in self.markers:
                    raise EvidenceError(f"{self.NAME} unexpectedly rebooted")
                self.markers.add(marker)
        self.console(line)
        if self.PREFIX not in line:
            return
        try:
            record = json.loads(line.split(self.PREFIX, 1)[1])
        except ValueError:
            record = None
        if not isinstance(record, dict):
            # A record cut by the reset, merged with the ROM banner or split by
            # another writer. It never counts as evidence and never starts or
            # extends a deadline; missing heartbeats still fail on the 20 s gap.
            if self.boot_id is None:
                self.before_boot += 1
                return
            self.damaged += 1
            if self.damaged > DAMAGED_LIMIT:
                raise EvidenceError(f"Repeated damaged {self.NAME} health telemetry "
                                    f"({self.damaged} records after boot)")
            return
        boot, uptime, event = record.get("boot_id"), record.get("uptime_ms"), record.get("event")
        if self.boot_id is None and event != "boot":
            # Output of the boot before the reset. Without this boot's record the
            # run still ends at the startup deadline as "No boot evidence".
            self.before_boot += 1
            return
        if type(boot) is not int or not 0 <= boot <= 0xffffffff or type(uptime) is not int or uptime < 0:
            raise EvidenceError("Invalid boot identity or device uptime")
        if event == "boot":
            if self.boot_id is not None or any(record.get(k) != self.expected[k] for k in IDENTITY):
                raise EvidenceError(f"{self.NAME} boot, hardware variant or running image differs from the candidate")
            self.booted(record)
            self.boot_id = boot
            self.last_uptime = uptime
            return
        if self.boot_id is None or self.boot_id != boot:
            raise EvidenceError(f"{self.NAME} did not produce fresh, continuous boot evidence")
        if event not in self.EVENTS:
            raise EvidenceError(f"Unknown {self.NAME} health event")
        if self.soak_started is not None and event in NETWORK_EVENTS:
            raise EvidenceError(f"{self.NAME} reconnected during the observation")
        if event == "wifi_connected":
            try:
                ip = ipaddress.IPv4Address(record.get("ip", ""))
            except ValueError as error:
                raise EvidenceError(f"{self.NAME} did not obtain IPv4") from error
            if ip.is_unspecified or ip.is_loopback or ip.is_link_local or ip.is_multicast or str(ip) == "255.255.255.255":
                raise EvidenceError(f"{self.NAME} did not obtain a usable IPv4 address")
            self.connected.add(event)
            self.seen.add(event)
        elif event == "mqtt_connected":
            if record.get("broker") != PRODUCTION_MQTT:
                raise EvidenceError(f"{self.NAME} connected to an unexpected MQTT broker")
            self.connected.add(event)
            self.seen.add(event)
        elif event == "cloud_ok":
            self.connected.add(event)
            self.seen.add(event)
        else:
            if uptime <= self.last_uptime:
                raise EvidenceError(f"{self.NAME} device uptime stopped advancing")
            self.last_uptime = uptime
            self.last_heartbeat = now
            self.heartbeats += 1
            age = record.get("menu_age_ms")
            menu_ready = record.get("main_menu") is True and type(age) is int and 0 <= age <= 20000
            self.menu_seen |= menu_ready
            self.menu_last = menu_ready
            if self.soak_started is not None and not menu_ready:
                raise EvidenceError(f"{self.NAME} menu task stopped making progress")
            ready = self.ready(record, menu_ready)
            if self.soak_started is None and ready and all(e in self.connected for e in self.REQUIRED):
                self.soak_started, self.soak_uptime = now, uptime
            if self.soak_started is not None:
                self.passed = now - self.soak_started >= Monitor.SOAK_SECONDS and uptime - self.soak_uptime >= Monitor.SOAK_SECONDS * 1000

    def extra_checks(self, check):
        return []


class LockboxMonitor(ProductionMonitor):
    """The Lockbox's ordinary production health protocol, including its production broker."""
    PREFIX, NAME = "LKBX_HEALTH ", "Lockbox"
    EVENTS = ("boot", "wifi_connected", "mqtt_connected", "heartbeat")
    REQUIRED = ("wifi_connected", "mqtt_connected")
    IMAGE_EVENTS = ("boot", "wifi_connected", "mqtt_connected", "heartbeat")
    STARTUP = "Wi-Fi, production MQTT and menu readiness did not arrive within 180 seconds"
    LOSSES = (("wi-fi station disconnected", ("wifi_connected", "mqtt_connected")),
              ("mqtt disconnected", ("mqtt_connected",)))


class OssmMonitor(ProductionMonitor):
    """OSSM_HEALTH for one HAT case: HAT mode at boot, then HAT detection and homing on every heartbeat."""
    PREFIX, NAME = "OSSM_HEALTH ", "OSSM"
    EVENTS = ("boot", "wifi_connected", "mqtt_connected", "heartbeat")
    # Staging and production OSSM builds share this broker, and the rig's OSSM
    # reaches it (production 1.0.67 reported a live MQTT session on the bench).
    REQUIRED = ("wifi_connected", "mqtt_connected")
    STARTUP = "Wi-Fi, production MQTT, HAT, homing and menu readiness did not arrive within 180 seconds"
    # The ESP-IDF MQTT client logs these errors in production builds when its session drops.
    LOSSES = tuple((text, ("mqtt_connected",)) for text in (
        "mqtt_client: error transport connect", "mqtt_client: can't send ping, disconnected",
        "mqtt_client: poll read error", "mqtt_client: writing failed"))

    def __init__(self, expected, started, case=WITH_HAT):
        super().__init__(expected, started)
        self.case = case
        self.hat_mode = None
        self.case_heartbeats = 0
        self.ossm_hat = None
        self.hat_seen = self.homed_seen = self.hat_last = self.homed_last = False

    def booted(self, record):
        mode = record.get("hat_mode")
        if mode not in ("auto", "off"):
            raise EvidenceError("OSSM boot did not report its HAT mode")
        if self.case and mode != self.case.mode:
            raise EvidenceError(f"OSSM booted with HAT mode {mode}, not {self.case.mode}")
        self.hat_mode = mode

    def console(self, line):
        if self.boot_id is None:
            return
        hat = parse_hat_status(line)
        if hat is None:
            return
        self.ossm_hat = hat
        if hat["guardLatched"]:
            fault = hat["guardFault"]
            reason = "StopPathUnverified (16)" if fault == STOP_PATH_UNVERIFIED else f"fault {fault}"
            raise EvidenceError("OSSM motor safety latch: " + reason)
        if self.case and not self.case.hat and hat["detected"]:
            raise EvidenceError("OSSM detected its HAT although HAT detection was off")
        if self.case and self.case.hat and self.soak_started is not None and hat["detected"] and not hat["passive"]:
            # Never qualified: the HAT as RS-485 bus master with serial motion.
            raise EvidenceError("OSSM HAT is driving the RS-485 bus instead of listening to the guard")

    def ready(self, record, menu_ready):
        hat, homed = record.get("hat"), record.get("homed")
        if type(hat) is not bool or type(homed) is not bool:
            raise EvidenceError("OSSM heartbeat did not report HAT detection and homing")
        if self.case and not self.case.hat and hat:
            raise EvidenceError("OSSM detected its HAT although HAT detection was off")
        self.hat_seen |= hat
        self.homed_seen |= homed
        self.hat_last, self.homed_last = hat, homed
        state = homed and (self.case is None or hat is self.case.hat)
        if self.soak_started is not None:
            if not homed:
                raise EvidenceError("OSSM lost its homed position during observation")
            if not state:
                raise EvidenceError("OSSM lost its HAT during observation")
        self.case_heartbeats += state
        return menu_ready and state

    def product_missing(self):
        phrases = []
        if self.case and self.case.hat:
            if not self.hat_seen:
                phrases.append("HAT never detected")
            elif not self.hat_last:
                phrases.append("HAT not detected at the deadline")
        if not self.homed_seen:
            phrases.append("carriage never homed")
        elif not self.homed_last:
            phrases.append("carriage not homed at the deadline")
        return phrases

    def extra_checks(self, check):
        if self.case is None:
            return [check("Carriage homed", self.case_heartbeats)]
        title = ("HAT detected and read-only under the guard, carriage homed" if self.case.hat
                 else "HAT detection off, carriage homed on the board's current sensor")
        return [check(f"HAT mode {self.case.mode} at boot", int(self.hat_mode == self.case.mode)),
                check(title, self.case_heartbeats)]


class TrainerMonitor(ProductionMonitor):
    PREFIX, NAME = "DTT_HEALTH ", "DTT"
    EVENTS = ("boot", "wifi_connected", "cloud_ok", "heartbeat")
    REQUIRED = ("wifi_connected", "cloud_ok")
    STARTUP = "Wi-Fi, production cloud health check and menu readiness did not arrive within 180 seconds"


class RadrMonitor(ProductionMonitor):
    PREFIX, NAME = "RADR_HEALTH ", "RADR"
    STARTUP = "Wi-Fi and menu readiness did not arrive within 180 seconds"


PRODUCTION_MONITORS = {"lkbx": LockboxMonitor, "ossm": OssmMonitor, "dtt": TrainerMonitor, "radr": RadrMonitor}


def observer_for(application, family):
    production = PRODUCTION_MONITORS.get(family)
    if production and all((production.PREFIX + '{"event":"' + event + '"').encode() in application
                          for event in production.IMAGE_EVENTS):
        return production
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


def case_checks(observer, outcome="success", detail="", prefix="", annotate=False):
    """Identity, network, product and soak checks for one observation."""
    def check(title, count):
        return {"title": prefix + title, "state": "passed" if count else "blocked", "evidenceCount": count}
    extra = []
    if isinstance(observer, ProductionMonitor):
        extra = [check(NETWORK_TITLES[event], int(event in observer.connected)) for event in observer.REQUIRED]
        extra += observer.extra_checks(check)
    seconds = 0 if observer.soak_started is None else int(observer.last_heartbeat - observer.soak_started)
    elapsed = 0 if observer.soak_uptime is None else int((observer.last_uptime - observer.soak_uptime) / 1000)
    reason = f"{seconds}s host time, {elapsed}s device time"
    ignored = [f"{count} {what} ignored" for count, what in (
        (getattr(observer, "damaged", 0), "damaged health records"),
        (getattr(observer, "before_boot", 0), "records before boot")) if count]
    if ignored:
        reason += "; " + ", ".join(ignored)
    soak = {"title": prefix + "Continuous application and network observation",
            "state": "passed" if observer.passed else "blocked", "evidenceCount": observer.heartbeats,
            "reason": reason}
    identity = check("Running image, board, build SHA and track", int(observer.boot_id is not None))
    if outcome == "failed" or (annotate and outcome == "blocked" and detail):
        attributed = identity if observer.boot_id is None else soak
        attributed["state"] = "failed" if outcome == "failed" else "blocked"
        attributed["reason"] = detail[:400]
    return [identity, *extra, soak]


def readback_check():
    return {"title": "Physical application SHA-256 readback", "state": "passed", "evidenceCount": 1}


def observations(observer, outcome="success", detail=""):
    return [readback_check(), *case_checks(observer, outcome, detail)]


def skipped_checks(case, reason, expected=None):
    """A HAT case that could not run: every check is blocked and says why."""
    rows = case_checks(OssmMonitor(expected or {}, 0, case), prefix=case.name + ": ")
    for row in rows:
        row.update(state="blocked", evidenceCount=0, reason=reason[:400])
    return rows
