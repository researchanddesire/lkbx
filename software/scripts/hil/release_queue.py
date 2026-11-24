"""Run the shared firmware validation queue on the enrolled Windows Test Rig."""
from __future__ import annotations

import argparse
from contextlib import ExitStack
import json
from hashlib import sha256
import logging
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from urllib.error import HTTPError
from urllib.request import HTTPRedirectHandler, Request, build_opener
from uuid import UUID

# The scheduled task uses -I and loads only this reviewed installation.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from catalog import PRODUCTS, REPOSITORIES, policy
from monitor import FAULT, EvidenceError, HAT_PREFIX, Monitor
from ossm_guard import GuardWatch, read_status, unsafe_reason
from release_health import (HAT_CASES, OssmMonitor, ReadinessNotReached, StartupTimeout, case_checks, observations,
                            observer_for, readback_check, runtime_digest, skipped_checks)
from run import esptool, find_port, load_fixture, observe, park, rig_lock, usb_path


ORIGINS = {"main": "https://dashboard.researchanddesire.com",
           "staging": "https://staging.researchanddesire.com"}
RIG_ROUTE = "/api/internal/firmware/v1/rig-queue"
BENCH_ORIGIN = "http://127.0.0.1:8766"
IDLE_SECONDS = 15
# The OSSM acknowledges a HAT setting at once; homing after a reset takes under a minute.
HAT_ACK_SECONDS = 20
HAT_SETTLE_SECONDS = 120
RELEASE_VARIANTS = {"ossm": {"v1": "4mb", "v2": "16mb"}, "radr": {"default": "r8"}}
PRODUCT_NAMES = {"lkbx": "Lockbox", "dtt": "DTT", "ossm": "OSSM", "radr": "Radar"}


class Blocked(Exception):
    """A fixture, operator setting or infrastructure prerequisite is missing."""


class ServiceError(Blocked):
    def __init__(self, code, route):
        self.code = code
        super().__init__(f"Service returned HTTP {code} for {route.split('?')[0]}")


class LeaseLost(Exception):
    """A fenced result can never be posted by this worker."""


class HatSwitchError(Blocked):
    """The running OSSM did not take a serial HAT setting."""


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, *args):
        return None


class Client:
    def __init__(self, origin, token, actor=None):
        self.origin = origin
        self.headers = {"Authorization": "Bearer " + token, "Content-Type": "application/json"}
        if actor:
            self.headers["X-Bench-Operator"] = actor
        self.http = build_opener(NoRedirect)

    def request(self, route, body=None):
        request = Request(self.origin + route,
                          None if body is None else json.dumps(body).encode(), self.headers)
        try:
            with self.http.open(request, timeout=15) as response:
                raw = response.read(4_000_001)
                if len(raw) > 4_000_000:
                    raise Blocked("Local service response exceeded the size limit")
                return json.loads(raw)
        except HTTPError as error:
            if route == RIG_ROUTE and error.code == 409:
                raise LeaseLost("Rig lease is no longer current") from error
            # No remote response bodies or credentials in logs.
            raise ServiceError(error.code, route) from error


class Bench(Client):
    def command(self, action, **args):
        return self.request("/command", {"action": action, "args": args})


class ReleaseFamilies:
    """Operator setting: the release families this rig runs release jobs for.

    `release-worker/release-families.json` is a JSON array such as ["radr"].
    Without the file every family runs. An invalid file pauses them all.
    """
    def __init__(self, allowed=None, error=None):
        self.allowed = allowed
        self.error = error

    @classmethod
    def load(cls, path):
        if not path.exists():
            return cls()
        try:
            # Windows PowerShell writes UTF-8 with a byte order mark.
            families = json.loads(path.read_text(encoding="utf-8-sig"))
        except (OSError, ValueError) as error:
            return cls(error=f"{path.name} could not be read ({type(error).__name__})")
        if not isinstance(families, list) or not all(isinstance(f, str) and f in PRODUCT_NAMES for f in families):
            return cls(error=f"{path.name} must be a JSON array of " + ", ".join(f'"{f}"' for f in PRODUCT_NAMES))
        return cls(allowed=frozenset(families))

    def refusal(self, family):
        """Why a release job for this family may not run, or None."""
        if self.error:
            return f"Release testing is paused on this rig: {self.error} (operator setting)"
        if self.allowed is not None and family not in self.allowed:
            return f"Release testing for {PRODUCT_NAMES.get(family, family)} is paused on this rig (operator setting)"
        return None


def enrolled_devices(fixture):
    targets = {}
    for repository, product in REPOSITORIES.items():
        try:
            devices = load_fixture(fixture, repository)
        except (KeyError, ValueError):
            # A partial enrollment does not prevent other products using the rig.
            continue
        for variant, device in devices.items():
            release_variant = {"4mb": "v1", "16mb": "v2"}.get(variant, variant)
            family = "lkbx" if product == "lockbox" else product
            targets[(family, "default" if family == "radr" else release_variant)] = device
            if product == "ossm" and variant == "16mb" and device.get("allow_larger_flash") is True:
                # The one enrolled OSSM serves both release lanes. Its V1 jobs
                # install the 4 MB profile; boot evidence still names the 16 MB chip.
                targets[("ossm", "v1")] = device
    return targets


def board_variant(family, release_variant):
    """The catalog variant (and boot `variant`) a release lane runs."""
    return RELEASE_VARIANTS.get(family, {}).get(release_variant, release_variant)


def allows_larger_flash(fixture, family, release_variant):
    """A smaller-flash image on this larger chip, approved per fixture (DTT V1, OSSM V1)."""
    product = "lockbox" if family == "lkbx" else family
    lane = PRODUCTS[product]["variants"][board_variant(family, release_variant)]
    return fixture.get("allow_larger_flash") is True and lane["flash_bytes"] < fixture["flash_bytes"]


def matching_board(boards, family, fixture):
    def matches(board):
        if not board["connected"] or board["family"] not in (family, "unknown"):
            return False
        if (board["vid"], board["pid"]) != (fixture["usb_vid"], fixture["usb_pid"]):
            return False
        if fixture.get("usb_serial"):
            return board["serial"] == fixture["usb_serial"]
        return usb_path(board["location"]) == usb_path(fixture["usb_location"])
    matched = [board for board in boards if matches(board)]
    return matched[0] if len(matched) == 1 else None


def bench_busy(status):
    return bool(status["activeRun"] or any(job["status"] == "running" for job in status["jobs"]))


def verify_board(record, fixture):
    identity = record.get("identity") or {}
    if (identity.get("macAddress", "").replace(":", "").upper() != fixture["device_id"]
            or identity.get("flashSizeBytes") != fixture["flash_bytes"]
            or identity.get("chipFamily", "").lower().replace("-", "") != fixture["chip"]
            or (fixture.get("psram") and identity.get("lkbxVariant") != fixture["psram"])):
        raise Blocked("Probed board identity or capacity differs from its enrollment")


def qualification(job):
    family, variant, version, sha = (job[key] for key in ("deviceType", "hardwareVariant", "version", "buildSha"))
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:[+-][A-Za-z0-9.-]+)?", version) or not re.fullmatch("[0-9a-f]{40}", sha):
        raise Blocked("Release version or build SHA is invalid")
    application = [a for a in job["artifacts"] if a["role"] == "application"]
    if len(application) != 1:
        raise Blocked("Registered application missing or ambiguous")
    asset = application[0]
    size, digest = asset["sizeBytes"], asset["sha256"]
    prefix = f"https://acjajruwevyyatztbkdf.supabase.co/storage/v1/object/public/{family}-firmware/releases/{version}/{sha}"
    suffix = "" if variant == "default" else "/" + variant
    if (asset["url"] != f"{prefix}{suffix}/firmware.bin" or type(size) is not int or not 0 < size <= 16*1024*1024
            or not re.fullmatch("[0-9a-f]{64}", digest)):
        raise Blocked("Application is outside the immutable release or has invalid metadata")
    with build_opener(NoRedirect).open(asset["url"], timeout=30) as response:
        data = response.read(size+1)
    if len(data) != size or sha256(data).hexdigest() != digest:
        raise Blocked("Downloaded application size or SHA-256 differs from the release")
    observer = observer_for(data, family)
    if observer is None:
        raise Blocked(
            f"{family.upper()} {version} lacks telemetry for a three-minute application/network observation. "
            "A reviewed production observer or an instrumented candidate is required."
        )
    if family == "ossm" and (observer is not OssmMonitor or b"next_boot" not in data):
        raise Blocked(f"OSSM {version} lacks the OSSM_HEALTH HAT and homing record or the serial HAT "
                      "switch that the With HAT and No HAT cases need.")
    return observer, runtime_digest(data), digest


def open_console(port_name):
    """The running application's console, opened without resetting the board."""
    import serial
    port = serial.Serial(port=None, baudrate=115200, timeout=.25, write_timeout=1)
    port.dtr = False; port.rts = False; port.port = port_name
    port.open()
    return port


def _record(line, prefix):
    if prefix not in line:
        return None
    try:
        record = json.loads(line.split(prefix, 1)[1])
    except ValueError:
        return None
    return record if isinstance(record, dict) else None


def switch_hat(port, mode, guard=None, clock=time.monotonic, ack_seconds=HAT_ACK_SECONDS,
               settle_seconds=HAT_SETTLE_SECONDS, resend_seconds=2):
    """Store an OSSM HAT mode for the next boot over its running console.

    The reply names the stored mode. A refused write (the ESP32 motor guard
    is armed, or NVS failed) adds "error" and names the unchanged mode, so
    only a reply naming the requested mode counts; refusals are retried until
    settle_seconds. After that the carriage must report it is homed (at most
    settle_seconds from the start), so the reset that follows never cuts a
    homing move. Returns whether the board reported homed.
    """
    started = clock()
    acknowledged = homed = False
    refused = None
    next_send = started
    pending = bytearray()
    while True:
        now = clock()
        if guard:
            guard.check()
        if acknowledged and (homed or now - started >= settle_seconds):
            return homed
        if not acknowledged:
            if now - started >= (settle_seconds if refused else ack_seconds):
                raise HatSwitchError(f"The OSSM refused 'hat {mode}' ({refused})" if refused else
                                     f"The OSSM did not acknowledge the serial command 'hat {mode}'")
            if now >= next_send:
                port.write(f"\nhat {mode}\n".encode())
                next_send = now + resend_seconds
        pending.extend(port.read(port.in_waiting or 1))
        if len(pending) > 65536:
            raise RuntimeError("serial line exceeded bounded framing")
        while b"\n" in pending:
            raw, _, rest = pending.partition(b"\n"); pending = bytearray(rest)
            line = raw.decode("utf-8", errors="replace").rstrip("\r")
            reply = _record(line, HAT_PREFIX)
            # The plain `hat` report also names a mode (this boot's); only the
            # switch reply says when it applies.
            if reply and reply.get("applies") == "next_boot" and not acknowledged:
                if reply.get("mode") == mode:
                    acknowledged = True
                else:
                    error = reply.get("error")
                    refused = error if error in ("motor_armed", "storage") else "a different mode is stored"
            heartbeat = _record(line, OssmMonitor.PREFIX)
            if heartbeat and heartbeat.get("event") == "heartbeat" and heartbeat.get("homed") is True:
                homed = True


def reset_board(port, device):
    """The same hardware reset observe() uses, on an open console."""
    from esptool.reset import HardReset
    HardReset(port, uses_usb=device["usb_vid"] == 0x303a and device["usb_pid"] == 0x1001)()


def guard_ready(guard):
    """Never reset the OSSM unless the guard is polling, unlatched and handed back.

    An OSSM that boots without the guard polling makes its HAT bus master and
    selects serial motion, which is not qualified on hardware.
    """
    guard.check()
    try:
        status = read_status(guard.path)
    except (OSError, ValueError, KeyError) as error:
        raise RuntimeError(f"RS-485 safety guard status unavailable: {error}") from error
    reason = unsafe_reason(status)
    if reason:
        raise RuntimeError("RS-485 safety guard: " + reason)


def hold_in_loader(port_name, log):
    """Park the OSSM in its ROM loader: no STEP pulses and no RS-485 traffic."""
    esptool(port_name, policy("researchanddesire/OSSM", "16mb"), ["read_mac"], log)


def radar_esptool(port_name, before, after, log, timeout=60):
    """read_mac on the rig's Radar with explicit reset handling. Returns the MAC it answered with."""
    command = [sys.executable, "-m", "esptool", "--chip", policy("researchanddesire/radr", "r8")["chip"],
               "--port", port_name, "--baud", "115200", "--before", before, "--after", after, "read_mac"]
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               encoding="utf-8", errors="replace", timeout=timeout)
    log.write(completed.stdout); log.flush()
    if completed.returncode:
        raise RuntimeError(f"esptool exited with {completed.returncode}")
    macs = re.findall(r"MAC:\s*([0-9a-fA-F:]{17})", completed.stdout)
    return macs[-1].replace(":", "").upper() if macs else None


class RadarHold:
    """Keep the rig's Radar in its ROM loader while an OSSM release runs.

    A running Radar connects to a booting OSSM over BLE and asks for a mode,
    which starts a second homing; the guard then latches on the repeated
    hard-stop contact. In the ROM loader the Radar has no radio. Release
    always resets it into its application and restarts any bench monitoring.
    """
    HELD = "Radar held in its bootloader so it cannot connect to the OSSM"
    PARKED = "OSSM parked in its bootloader before the Radar was released"
    RELEASED = "Radar released into its application afterwards"
    FAILURES = (Blocked, RuntimeError, OSError, ValueError, subprocess.SubprocessError)

    def __init__(self, fixture, bench, board, log_path, park_ossm=None):
        self.fixture = fixture
        self.bench = bench
        self.board = board
        self.log_path = log_path
        # Parks the OSSM in its ROM loader; the Radar is released only after it.
        self.park_ossm = park_ossm
        self.attempted = self.held = self.monitoring = self.done = self.parked = False
        self.error = self.park_error = None

    def hold(self):
        port_name = find_port(self.fixture)
        if self.board and self.board.get("monitoring"):
            self.bench.command("monitor.stop", deviceId=self.board["id"])
            self.monitoring = True
        self.log_path.parent.mkdir(parents=True, exist_ok=True)
        self.attempted = True
        with self.log_path.open("a", encoding="utf-8") as log:
            mac = radar_esptool(port_name, "default_reset", "no_reset", log)
        if mac != self.fixture["device_id"]:
            raise Blocked("the board on the Radar's enrolled port did not answer with the Radar's factory MAC")
        self.held = True

    def release(self):
        """Idempotent; never raises, so every other cleanup step still runs."""
        if self.done:
            return self.error
        self.done = True
        errors = []
        if self.attempted and self.park_ossm:
            try:
                self.park_ossm()
                self.parked = True
            except self.FAILURES as error:
                # A free Radar beside a running OSSM is what this hold prevents.
                self.park_error = str(error)
                self.error = f"the OSSM could not be parked ({error}), so the Radar was left in its bootloader"
                logging.warning("Radar release: %s", self.error)
                return self.error
        if self.attempted:
            try:
                with self.log_path.open("a", encoding="utf-8") as log:
                    radar_esptool(find_port(self.fixture), "default_reset", "hard_reset", log)
            except self.FAILURES as error:
                errors.append(f"reset into its application failed ({error})")
        if self.monitoring:
            try:
                self.bench.command("monitor.start", deviceId=self.board["id"])
            except self.FAILURES as error:
                errors.append(f"bench monitoring did not restart ({error})")
        self.error = "; ".join(errors) or None
        if self.error:
            logging.warning("Radar release: %s", self.error)
        return self.error

    def checks(self):
        released = {"title": self.RELEASED, "state": "passed" if self.done and not self.error else "blocked",
                    "evidenceCount": int(self.done and not self.error)}
        if self.error:
            released["reason"] = self.error[:400]
        parked = {"title": self.PARKED, "state": "passed" if self.parked else "blocked", "evidenceCount": int(self.parked)}
        if self.park_error:
            parked["reason"] = self.park_error[:400]
        return ({"title": self.HELD, "state": "passed" if self.held else "blocked", "evidenceCount": int(self.held)},
                parked, released)


class RestoreObserver:
    """The boot after restoring `hat auto`: HAT detection on, then the carriage homed."""
    def __init__(self, expected, started):
        self.expected = expected
        self.started = started
        self.boot_id = None
        self.passed = False

    def tick(self, now):
        if now - self.started > Monitor.STARTUP_SECONDS:
            raise EvidenceError("The restored boot did not report HAT mode auto and a homed carriage within 180 seconds")

    def feed(self, line, now):
        self.tick(now)
        if FAULT.search(line):
            raise EvidenceError("The OSSM reported a reset, panic, watchdog or brownout after the restore")
        record = _record(line, OssmMonitor.PREFIX)
        if not record:
            return
        if record.get("event") == "boot" and self.boot_id is None:
            if record.get("device_id") != self.expected["device_id"] or record.get("hat_mode") != "auto":
                raise EvidenceError("The restored boot did not report HAT mode auto on the enrolled board")
            self.boot_id = record.get("boot_id")
        elif (record.get("event") == "heartbeat" and self.boot_id is not None
              and record.get("boot_id") == self.boot_id and record.get("homed") is True):
            self.passed = True


class Worker:
    def __init__(self, queue, bench, home, runner, motion_devices=(), sleep=time.sleep, release_families=None):
        self.queue = queue
        self.bench = bench
        self.home = home
        self.runner = runner
        self.motion = set(motion_devices)
        self.families = release_families or ReleaseFamilies()
        self.sleep = sleep
        # ACL-protected directory outside the GitHub runner's writable directories.
        self.state = home / "release-worker" / "current.json"
        self.receipt = home / "release-admission" / "admitted.json"

    def call(self, action, **fields):
        return self.queue.request(RIG_ROUTE, dict(action=action, runner=self.runner, **fields))

    def remember(self, data):
        self.state.parent.mkdir(exist_ok=True)
        temp = self.state.with_suffix(".tmp")
        temp.write_text(json.dumps(data), encoding="utf-8")
        os.replace(temp, self.state)

    @staticmethod
    def fence(job):
        if job.get("source") == "pull_request":
            return {"jobId": job["jobId"], "leaseToken": job["leaseToken"]}
        return {"releaseId": job["releaseId"], "leaseToken": job["leaseToken"]}

    def heartbeat(self, job):
        if not self.call("heartbeat", jobId=job["jobId"], leaseToken=job["leaseToken"]).get("ok"):
            raise LeaseLost("Rig lease ended")

    def wait_operation(self, started, guard):
        while True:
            # A flash/probe runs in the persistent agent. Keep the physical lock until
            # it has actually exited, including when the portal is briefly unreachable.
            status = self.until_reachable(lambda: self.bench.request("/status"))
            task = next((j for j in status["jobs"] if j["id"] == started["id"]), None)
            if task is None:
                if not bench_busy(status):
                    raise Blocked("Bench lost the operation record")
            elif task["status"] != "running":
                if task["status"] != "completed":
                    raise Blocked(str(task.get("message", "Fixture operation failed"))[:400])
                if guard:
                    guard.check()
                return task, status
            self.sleep(2)

    def until_reachable(self, operation):
        while True:
            try:
                return operation()
            except ServiceError as error:
                if error.code == 404:
                    raise Blocked("Bench operation evidence was lost") from error
                self.sleep(5)
            except (OSError, Blocked, ValueError):
                self.sleep(5)

    def drain(self):
        # A command may reach the bench even if its reply is lost. Never release
        # a hardware lock until the independent local agent confirms it is idle.
        while bench_busy(self.until_reachable(lambda: self.bench.request("/status"))):
            self.sleep(2)

    def release_test(self, job, targets, status):
        # Before any download, lock, flash or other hardware step.
        paused = self.families.refusal(job.get("deviceType"))
        if paused:
            raise Blocked(paused)
        if job.get("kind") != "firmware" or job["track"] != "main":
            raise Blocked("Production Test Bench requires a registered main firmware release")
        family = job["deviceType"]
        fixture = targets.get((family, job["hardwareVariant"]))
        if not fixture:
            raise Blocked("Requested board variant is not enrolled")
        board = matching_board(status["devices"], family, fixture)
        if not board:
            raise Blocked("Enrolled USB board is disconnected, ambiguous or unassigned in Test Bench")
        if status.get("fixtureValidationProtocol") != 1:
            raise Blocked("Install the reviewed Test Bench release fixture adapter")
        observer_type, runtime_sha, app_sha = qualification(job)
        if family == "ossm" and fixture["device_id"] not in self.motion:
            raise Blocked("Starting an OSSM release requires operator-enabled homing on this enrolled fixture")
        board_id = board["id"]
        locks = self.home / "locks"
        owner = "release " + str(UUID(job["releaseId"]))
        with ExitStack() as stack:
            # Protect a started run from watchdog preemption, but never preempt
            # an existing holder. Manual and legacy PR tools use these same files.
            stack.enter_context(rig_lock(locks / (fixture["product"] + ".lock"), priority=True, owner=owner))
            if family == "lkbx":
                stack.enter_context(rig_lock(Path("C:/lkbx-hil/fixture.lock"), priority=True, owner=owner))
            radar_fixture = None
            if family == "ossm":
                # Never run an OSSM unattended beside a free Radar (see RadarHold).
                radar_fixture = targets.get(("radr", "default"))
                if not radar_fixture:
                    raise Blocked("The rig's Radar is not enrolled, so it cannot be held away from the OSSM")
                try:
                    stack.enter_context(rig_lock(locks / (radar_fixture["product"] + ".lock"), priority=True, owner=owner))
                except RuntimeError as error:
                    raise Blocked("The rig's Radar is in use by another run (radr.lock is held), "
                                  "so it cannot be held away from the OSSM") from error
            self.bench.command("control.claim")
            stack.callback(self.bench.command, "control.release")
            claimed = self.bench.request("/status")
            if bench_busy(claimed):
                raise Blocked("Another bench operation started before the fixture was claimed")
            guard = radar = None
            if family == "ossm":
                guard = GuardWatch(self.home / "rs485-guard" / "status.json",
                                   self.state.parent / str(UUID(job["releaseId"])))
                stack.callback(guard.close)
                guard.require()  # Do not clear latches or trigger guard recovery for a release.
                # Before the probe, flash or any reset of the OSSM; held through both
                # HAT cases and the restore, and released on every path out.
                hold_log = self.state.parent / str(UUID(job["releaseId"])) / "radar-hold.log"
                def park_ossm():
                    # After the hat-auto restore; a Radar released next finds no running OSSM.
                    with hold_log.open("a", encoding="utf-8") as log:
                        park(fixture, policy("researchanddesire/OSSM", "16mb"), log)
                radar = RadarHold(radar_fixture, self.bench, matching_board(claimed["devices"], "radr", radar_fixture),
                                  hold_log, park_ossm)
                stack.callback(radar.release)
                try:
                    radar.hold()
                except RadarHold.FAILURES as error:
                    raise Blocked(f"The rig's Radar could not be held in its bootloader ({error}), "
                                  "so the OSSM was not started") from error
            stack.callback(self.drain)
            def before_command():
                self.heartbeat(job)
                self.bench.command("control.renew")
                if guard:
                    guard.check()
            before_command()
            if board["family"] == "unknown":
                self.bench.command("device.configure", deviceId=board_id,
                                   mapping={"label": board["label"], "family": family})
            _, probed = self.wait_operation(self.bench.command("device.probe", deviceId=board_id), guard)
            current = matching_board(probed["devices"], family, fixture)
            if not current or current["id"] != board_id:
                raise Blocked("Board disconnected or USB mapping changed after probing")
            verify_board(current, fixture)
            before_command()
            larger = allows_larger_flash(fixture, family, job["hardwareVariant"])
            flash = self.bench.command("firmware.flash", deviceId=board_id, releaseId=job["releaseId"],
                                       confirm=True, erase=False, fixtureValidation={
                                           "macAddress": fixture["device_id"], "variant": job["hardwareVariant"],
                                           "buildSha": job["buildSha"], "version": job["version"], "artifacts": job["artifacts"],
                                           "applicationSha256": app_sha,
                                           # The current Test Bench reads allowLargerFlash; older ones the DTT name.
                                           "allowLargerFlash": larger, "allowDttV1On16Mb": larger})
            completed, _ = self.wait_operation(flash, guard)
            if completed.get("verifiedApplicationSha256") != app_sha:
                raise Blocked("Bench did not return verified application readback")
            before_command()
            if board.get("monitoring"):
                self.bench.command("monitor.stop", deviceId=board_id)
                def resume_monitoring():
                    # Reopening the port of a parked OSSM could reset it into its application.
                    if not (radar and radar.parked):
                        self.bench.command("monitor.start", deviceId=board_id)
                stack.callback(resume_monitoring)
            # Like DTT V1 on its 16 MB board, an OSSM V1 job boots as variant 4mb
            # while the board's identity keeps its measured 16 MB capacity.
            expected = dict(schema=1, product=fixture["product"], variant=board_variant(family, job["hardwareVariant"]),
                            device_id=fixture["device_id"], flash_bytes=fixture["flash_bytes"],
                            build_sha=job["buildSha"], image_sha256=runtime_sha, track=job["track"])
            evidence = self.state.parent / str(UUID(job["releaseId"]))
            base = {"board": board_id, "applicationSha256": app_sha, "runtimeImageSha256": runtime_sha,
                    "suite": "release-health"}
            if family == "ossm":
                outcome, detail, checks, cases = self.hat_cases(fixture, observer_type, expected, guard,
                                                                before_command, evidence)
                # Released here, after the restore, so the evidence records it; the
                # cleanup callback then has nothing left to do.
                if radar.release():
                    detail += f" The Radar was not released: {radar.error}."
                held, parked, released = radar.checks()
                checks = [checks[0], held, *checks[1:], parked, released]
                return outcome, {**base, "detail": detail[:400], "checks": checks, "cases": cases}
            observer = observer_type(expected, time.monotonic())
            outcome, detail = self.observe_case(fixture, observer, guard, evidence / "serial.log")
            if outcome == "success":
                detail = "Verified application readback, running identity and 180-second hardware observation"
            return outcome, {**base, "detail": detail[:400], "checks": observations(observer, outcome, detail)}

    @staticmethod
    def observe_case(fixture, observer, guard, log_path):
        """Reset the board and observe it. Returns (outcome, detail)."""
        try:
            # The same serial reset, line framing, fault detection, heartbeat
            # and guard polling used by the reviewed PR harness. The local log
            # keeps only the redacted, credential-free records.
            log_path.parent.mkdir(parents=True, exist_ok=True)
            with log_path.open("w", encoding="utf-8") as log:
                observe(fixture, find_port(fixture), observer, log, guard)
            if guard:
                guard.check()
            if not observer.passed:
                raise EvidenceError("Required physical observation did not complete")
        except EvidenceError as error:
            detail = str(error)
            if observer.boot_id is None and (isinstance(error, StartupTimeout) or detail == "startup deadline exceeded"):
                return "blocked", "No boot evidence was received. Check the enrolled UART and fixture power."
            if isinstance(error, ReadinessNotReached):
                # The board ran and stayed healthy; something it needs (production
                # MQTT, the cloud, its main menu) never arrived. Not a firmware fault.
                return "blocked", detail
            return "failed", detail
        except (RuntimeError, OSError) as error:
            return "blocked", str(error)
        return "success", ""

    def prepare_case(self, fixture, mode, guard, renew):
        renew()
        guard_ready(guard)
        port = open_console(find_port(fixture))
        try:
            try:
                switch_hat(port, mode, guard)
            except HatSwitchError:
                # The armed motor guard refuses flash writes. Write the setting in the
                # seconds after a reset, while the OSSM is still discovering its HAT and
                # before it homes; that boot still homes in full before the case's reset.
                guard_ready(guard)
                reset_board(port, fixture)
                switch_hat(port, mode, guard, ack_seconds=15, resend_seconds=0.5)
        finally:
            port.close()
        # Checked again right before the reset that applies the setting.
        guard_ready(guard)

    def hat_cases(self, fixture, observer_type, expected, guard, renew, evidence):
        """Run With HAT, then No HAT, on the one OSSM; always restore `hat auto` after."""
        results = []
        try:
            for case in HAT_CASES:
                if results and results[-1][2] == "blocked":
                    results.append((case, None, "blocked", f"Not run: {results[-1][0].name} could not run"))
                    continue
                try:
                    self.prepare_case(fixture, case.mode, guard, renew)
                except (Blocked, RuntimeError, OSError) as error:
                    results.append((case, None, "blocked", str(error)))
                    continue
                observer = observer_type(expected, time.monotonic(), case)
                slug = case.name.lower().replace(" ", "-")
                outcome, detail = self.observe_case(fixture, observer, guard, evidence / f"{slug}-serial.log")
                results.append((case, observer, outcome, detail))
        finally:
            # Also after a failure or a lost lease: PR runs and the next job expect HAT detection.
            restore_error = self.restore_hat(fixture, expected, guard, renew, evidence)
        outcomes = [outcome for _, _, outcome, _ in results]
        outcome = "failed" if "failed" in outcomes else "blocked" if "blocked" in outcomes else "success"
        checks = [readback_check()]
        for case, observer, case_outcome, detail in results:
            prefix = case.name + ": "
            checks += (case_checks(observer, case_outcome, detail, prefix, annotate=True) if observer
                       else skipped_checks(case, detail, expected))
        restored = {"title": "HAT detection restored for the next run", "state": "passed" if not restore_error else "blocked",
                    "evidenceCount": int(not restore_error)}
        if restore_error:
            restored["reason"] = restore_error[:400]
        checks.append(restored)
        if outcome == "success":
            detail = ("Verified application readback, running identity and 180-second hardware observations "
                      "With HAT and No HAT")
        else:
            detail = "; ".join(f"{case.name}: {d}" for case, _, o, d in results if o != "success")
        if restore_error:
            detail = f"HAT auto was not restored ({restore_error}). " + detail
            # The release may be fine, but the fixture needs service before another run.
            outcome = "blocked" if outcome == "success" else outcome
        cases = [{"name": case.name, "hatMode": case.mode, "outcome": o} for case, _, o, _ in results]
        return outcome, detail, checks, cases

    def restore_hat(self, fixture, expected, guard, renew, evidence):
        """Leave the OSSM in `hat auto` and homed. Returns None, or why it could not."""
        try:
            evidence.mkdir(parents=True, exist_ok=True)
            port_name = find_port(fixture)
            try:
                guard_ready(guard)
            except RuntimeError as error:
                # Never boot it unguarded. Park it instead; nothing moves until a person or PR run recovers the guard.
                with (evidence / "restore-hold.log").open("w", encoding="utf-8") as log:
                    hold_in_loader(port_name, log)
                return f"{error}. The OSSM is held in its ROM loader and still needs hat auto"
            try:
                renew()
            except (Blocked, LeaseLost):
                pass  # Restore the board even when the result can no longer be posted.
            self.prepare_case(fixture, "auto", guard, lambda: None)
            observer = RestoreObserver(expected, time.monotonic())
            with (evidence / "restore-serial.log").open("w", encoding="utf-8") as log:
                observe(fixture, find_port(fixture), observer, log, guard)
            guard.check()
            return None
        except (Blocked, EvidenceError, RuntimeError, OSError) as error:
            return str(error) or type(error).__name__

    def process(self, job, targets, status):
        from threading import Event, Thread
        stop = Event()
        def keepalive():
            while not stop.wait(10):
                try:
                    self.heartbeat(job)
                    # During a long flash the local operator claim must stay live.
                    self.bench.command("control.renew")
                except (LeaseLost, Blocked, OSError, ValueError):
                    # All new physical operations fence synchronously. An in-flight
                    # flash/suite retains its physical lock through terminal cleanup.
                    pass
        thread = Thread(target=keepalive, daemon=True)
        thread.start()
        try:
            try:
                return self.release_test(job, targets, status)
            except (Blocked, RuntimeError, OSError, ValueError) as error:
                return "blocked", {"detail": str(error)[:400] or "Fixture operation did not finish"}
        finally:
            stop.set()
            thread.join()

    def finish(self, job, status, summary):
        self.call("complete", **self.fence(job), deviceType=job["deviceType"],
                  hardwareVariant=job["hardwareVariant"], buildSha=job["buildSha"], status=status, summary=summary)

    def tick(self):
        state = json.loads(self.state.read_text()) if self.state.exists() else None
        # A PR is executed by its pinned GitHub wrapper. Its hosted completion
        # closes this lease. Even a worker restart cannot start another flash.
        if state and state["job"].get("source") == "pull_request":
            try:
                self.heartbeat(state["job"])
            except LeaseLost:
                self.receipt.unlink(missing_ok=True)
                self.state.unlink()
                return
            job = state["job"]
            expected = {key: job[key] for key in ("repository", "buildSha", "runId", "runAttempt")}
            try:
                authorized = self.call("authorize-pr", **expected)
            except LeaseLost:
                self.receipt.unlink(missing_ok=True)
                return
            if authorized.get("ok"):
                temp = self.receipt.with_suffix(".tmp")
                temp.write_text(json.dumps({**expected, "expiresAt": time.time()+50}), encoding="utf-8")
                os.replace(temp, self.receipt)
            return
        status = self.bench.request("/status")
        if bench_busy(status):
            return
        if state:
            job = state["job"]
            result = state.get("result", ["blocked", {"detail": "Worker restarted during qualification; inspect bench evidence before retry"}])
            try:
                self.finish(job, *result)
            except LeaseLost:
                pass
            self.state.unlink()
            return
        if status["operator"]:
            return
        if not (self.receipt.parent / "enabled").is_file():
            # Empty lane advertisements record presence without reserving a job.
            self.call("claim", capabilities=[])
            return
        targets = enrolled_devices(self.home / "fixture.json")
        capabilities = [{"deviceType": family, "hardwareVariant": variant}
                        for (family, variant), fixture in targets.items()
                        if matching_board(status["devices"], family, fixture)]
        job = self.call("claim", capabilities=capabilities).get("job")
        if not job:
            return
        self.remember({"job": job})
        if job.get("source") == "pull_request":
            return
        result = self.process(job, targets, status)
        self.remember({"job": job, "result": result})
        try:
            self.finish(job, *result)
        except LeaseLost:
            pass
        self.state.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--home", type=Path, default=Path("C:/rad-hil"))
    parser.add_argument("--track", choices=ORIGINS, required=True)
    parser.add_argument("--runner", default="desktop-sgs6goq-lkbx-rig-01")
    parser.add_argument("--once", action="store_true")
    args = parser.parse_args()
    directory = Path(os.environ.get("BENCH_DATA_DIR") or
                     Path(os.environ["LOCALAPPDATA"]) / "ResearchAndDesire" / "TestBench")
    config = args.home / "release-worker"
    token = os.environ["FIRMWARE_VALIDATION_RIG_TOKEN"]
    if not token or not re.fullmatch(r"[\w.-]{1,128}", args.runner):
        parser.error("A configured rig name and token are required")
    motion = config / "motion-devices.json"
    motion_devices = json.loads(motion.read_text(encoding="utf-8-sig")) if motion.exists() else []
    families = ReleaseFamilies.load(config / "release-families.json")
    if families.error:
        logging.warning("Release jobs are paused: %s", families.error)
    bench = Bench(BENCH_ORIGIN, (directory / "agent.token").read_text().strip(), "firmware-queue:" + args.runner)
    worker = Worker(Client(ORIGINS[args.track], token), bench, args.home, args.runner, motion_devices,
                    release_families=families)
    with rig_lock(config / "worker.lock"):
        while True:
            try:
                worker.tick()
            except (Blocked, LeaseLost, OSError, ValueError) as error:
                logging.warning("Queue temporarily unavailable (%s)", type(error).__name__)
                if args.once:
                    raise
            if args.once:
                return
            time.sleep(IDLE_SECONDS)


if __name__ == "__main__":
    main()
