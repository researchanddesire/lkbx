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
import sys
import time
from urllib.error import HTTPError
from urllib.request import HTTPRedirectHandler, Request, build_opener
from uuid import UUID

# The scheduled task uses -I and loads only this reviewed installation.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from catalog import REPOSITORIES
from monitor import EvidenceError
from ossm_guard import GuardWatch
from release_health import observations, observer_for, runtime_digest
from run import find_port, load_fixture, observe, rig_lock, usb_path


ORIGINS = {"main": "https://dashboard.researchanddesire.com",
           "staging": "https://staging.researchanddesire.com"}
RIG_ROUTE = "/api/internal/firmware/v1/rig-queue"
BENCH_ORIGIN = "http://127.0.0.1:8766"
IDLE_SECONDS = 15


class Blocked(Exception):
    """A fixture, operator setting or infrastructure prerequisite is missing."""


class ServiceError(Blocked):
    def __init__(self, code, route):
        self.code = code
        super().__init__(f"Service returned HTTP {code} for {route.split('?')[0]}")


class LeaseLost(Exception):
    """A fenced result can never be posted by this worker."""


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
    return targets


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
    return observer, runtime_digest(data), digest


class Worker:
    def __init__(self, queue, bench, home, runner, motion_devices=(), sleep=time.sleep):
        self.queue = queue
        self.bench = bench
        self.home = home
        self.runner = runner
        self.motion = set(motion_devices)
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
            self.bench.command("control.claim")
            stack.callback(self.bench.command, "control.release")
            if bench_busy(self.bench.request("/status")):
                raise Blocked("Another bench operation started before the fixture was claimed")
            guard = None
            if family == "ossm":
                guard = GuardWatch(self.home / "rs485-guard" / "status.json",
                                   self.state.parent / str(UUID(job["releaseId"])))
                stack.callback(guard.close)
                guard.require()  # Do not clear latches or trigger guard recovery for a release.
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
            flash = self.bench.command("firmware.flash", deviceId=board_id, releaseId=job["releaseId"],
                                       confirm=True, erase=False, fixtureValidation={
                                           "macAddress": fixture["device_id"], "variant": job["hardwareVariant"],
                                           "buildSha": job["buildSha"], "version": job["version"], "artifacts": job["artifacts"],
                                           "applicationSha256": app_sha,
                                           "allowDttV1On16Mb": fixture.get("allow_larger_flash") is True})
            completed, _ = self.wait_operation(flash, guard)
            if completed.get("verifiedApplicationSha256") != app_sha:
                raise Blocked("Bench did not return verified application readback")
            before_command()
            if board.get("monitoring"):
                self.bench.command("monitor.stop", deviceId=board_id)
                stack.callback(self.bench.command, "monitor.start", deviceId=board_id)
            expected = dict(schema=1, product=fixture["product"], variant=(
                {"v1":"4mb","v2":"16mb"}[job["hardwareVariant"]] if family == "ossm"
                else "r8" if family == "radr" else job["hardwareVariant"]),
                device_id=fixture["device_id"], flash_bytes=fixture["flash_bytes"],
                build_sha=job["buildSha"], image_sha256=runtime_sha, track=job["track"])
            observer = observer_type(expected, time.monotonic())
            outcome, detail = "success", "Verified application readback, running identity and 180-second hardware observation"
            try:
                # The same serial reset, line framing, fault detection, heartbeat
                # and guard polling used by the reviewed PR harness.
                with open(os.devnull, "w", encoding="utf-8") as log:
                    observe(fixture, find_port(fixture), observer, log, guard)
                if guard:
                    guard.check()
                if not observer.passed:
                    raise EvidenceError("Required physical observation did not complete")
            except EvidenceError as error:
                detail = str(error)
                no_boot = observer.boot_id is None and detail in {
                    "startup deadline exceeded",
                    "Wi-Fi, production MQTT and menu readiness did not arrive within 180 seconds"
                }
                if no_boot:
                    outcome, detail = "blocked", "No boot evidence was received. Check the enrolled UART and fixture power."
                else:
                    outcome = "failed"
            except (RuntimeError, OSError) as error:
                outcome, detail = "blocked", str(error)
            return outcome, {"board": board_id, "applicationSha256": app_sha,
                             "runtimeImageSha256": runtime_sha, "detail": detail[:400],
                             "suite": "release-health", "checks": observations(observer, outcome, detail)}

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
    motion_devices = json.loads(motion.read_text()) if motion.exists() else []
    bench = Bench(BENCH_ORIGIN, (directory / "agent.token").read_text().strip(), "firmware-queue:" + args.runner)
    worker = Worker(Client(ORIGINS[args.track], token), bench, args.home, args.runner, motion_devices)
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
