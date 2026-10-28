#!/usr/bin/env python3
"""Local-only HTTPS fixture with per-device identities and in-memory sessions."""

from __future__ import annotations

import argparse
import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
from pathlib import Path
import re
import socket
import ssl
import struct
import threading
from urllib.parse import unquote, urlsplit
import urllib.request

MAX_BODY = 16384


def fresh_session(identity: str) -> dict:
    return {"sessionId": "local-" + identity.replace(":", "").lower(), "lockState": "unlocked", "lockStart": 0,
            "lockEnd": 0, "duration": 0, "isBreak": False, "breakMax": 0, "breakReg": 0, "breakLast": 0,
            "isEUnlock": True, "isTestLock": True, "isTimeDisp": True, "displayElapsedTime": False}


def no_update(report: dict) -> tuple[int, dict]:
    """Local services never offer firmware, so development devices never fetch a cloud artifact."""
    track = report.get("reportedTrack")
    version = report.get("currentVersion")
    if track not in ("main", "staging") or not isinstance(version, str) or not re.fullmatch(r"\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?", version):
        return 400, {"error": "invalid_report"}
    return 200, {"protocolVersion": 1, "shouldUpdate": False, "updateAvailable": False, "reason": "no-target",
                 "reportedTrack": track, "assignedTrack": track, "trackChanged": False, "currentVersion": version,
                 "targetVersion": None, "nextHopVersion": None, "update": None, "nextCheckSeconds": 3600,
                 "firmwareOrigin": "community"}


class Fixture:
    def __init__(self, config: dict, publish=None):
        self.config = copy.deepcopy(config)
        self.devices = self.config["devices"]
        self.sessions = {identity: fresh_session(identity) for identity in self.devices}
        self.paired = {identity: True for identity in self.devices}
        self.telemetry = {}
        self.publish = publish
        self.lock = threading.RLock()

    def principal(self, authorization: str):
        if not authorization.startswith("Bearer "):
            return False
        token = authorization[7:]
        if not token.isascii() or len(token) > 128:
            return False
        if hmac.compare_digest(token, self.config["controller_api_token"]):
            return "controller"
        for identity, credential in self.devices.items():
            if hmac.compare_digest(token, credential["api_token"]):
                return identity
        return False

    def device_allowed(self, principal, identity):
        return isinstance(identity, str) and identity in self.devices and principal in ("controller", identity)

    def dispatch(self, method: str, raw_path: str, authorization: str, body: dict) -> tuple[int, dict]:
        principal = self.principal(authorization)
        if not principal:
            return 401, {"error": "unauthorized"}
        parsed = urlsplit(raw_path)
        if parsed.scheme or parsed.netloc or parsed.query or parsed.fragment:
            return 400, {"error": "invalid_route"}
        path = unquote(parsed.path)
        if method == "GET" and path in ("/api/health", "/api/health-check"):
            return 200, {"ok": True, "fixture": True}
        if path in ("/api/lockbox/auth", "/api/lockbox/is-paired", "/api/lockbox/test-pair") and method == "POST":
            identity = body.get("mac") if path.endswith("/auth") else body.get("macAddress")
            if not self.device_allowed(principal, identity):
                return 403, {"error": "device_denied"}
            with self.lock:
                if path.endswith("/auth"):
                    credential = self.devices[identity]
                    return 200, {"clientId": identity, "username": credential["username"], "password": credential["password"],
                                 "sessionId": self.sessions[identity]["sessionId"], "pairingCode": credential["pairing_code"],
                                 "hueShift": 0, "isPaired": self.paired[identity]}
                if path.endswith("/test-pair"):
                    self.paired[identity] = True
                    return 200, {"ok": True}
                if not self.paired[identity]:
                    return 418, {"error": "unpaired"}
                return 200, {"macAddress": identity, "bubbleId": "local-bubble-" + identity.replace(":", "").lower()}
        if path.startswith("/api/lockbox/state/") and method in ("GET", "POST"):
            identity = path.removeprefix("/api/lockbox/state/")
            if not self.device_allowed(principal, identity):
                return 403, {"error": "device_denied"}
            with self.lock:
                if method == "POST":
                    # Device telemetry is not permission to replace fixture-owned session policy.
                    allowed = {"id", "state", "createdAt", "firmwareProvenanceId", "isLocked", "isLockTimer", "isEBlocked", "isLocalPlay", "emergencyEnd", "sessionId", "breakLast"}
                    if set(body) - allowed:
                        return 400, {"error": "unsupported_telemetry"}
                    self.telemetry[identity] = copy.deepcopy(body)
                session = copy.deepcopy(self.sessions[identity])
                return 200, {"ok": True, "clear": session["lockState"] == "unlocked", "data": session}
        if path == "/api/firmware/v1/check" and method == "POST":
            if not self.device_allowed(principal, body.get("deviceId")) or body.get("deviceType") != "lkbx":
                return 403, {"error": "device_denied"}
            return no_update(body)
        if path.startswith("/api/local/v1/session/") and method == "PUT":
            if principal != "controller":
                return 403, {"error": "controller_required"}
            identity = path.removeprefix("/api/local/v1/session/")
            if identity not in self.devices:
                return 404, {"error": "unknown_fixture"}
            session = fresh_session(identity)
            if set(body) - set(session) or body.get("lockState") not in ("locked", "unlocked", "break"):
                return 400, {"error": "invalid_session"}
            for key, value in body.items():
                expected = type(session[key])
                if type(value) is not expected or (expected is int and not 0 <= value <= 2**63 - 1) or (expected is str and len(value) > 80):
                    return 400, {"error": "invalid_session"}
            session.update(body)
            message = {"type": "sync", "clear": session["lockState"] == "unlocked", **session}
            # The HTTP snapshot stores milliseconds; the MQTT sync protocol
            # carries duration in seconds, matching the real dashboard.
            message["duration"] = session["duration"] // 1000
            try:
                if self.publish:
                    self.publish(identity, message)
            except (OSError, ValueError):
                return 503, {"error": "local_broker_unavailable"}
            with self.lock:
                self.sessions[identity] = session
            return 200, {"ok": True, "data": copy.deepcopy(session)}
        return 404, {"error": "not_found"}


def handler_for(fixture: Fixture):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def setup(self):
            super().setup()
            self.connection.settimeout(5)

        def log_message(self, format, *args):
            # Requests can contain credentials or session data; omit request logging.
            pass

        def send_json(self, status, value):
            encoded = json.dumps(value, separators=(",", ":")).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(encoded)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(encoded)
            self.close_connection = True

        def route(self):
            if self.headers.get("Transfer-Encoding"):
                self.send_json(400, {"error": "unsupported_encoding"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
            except ValueError:
                self.send_json(400, {"error": "invalid_length"})
                return
            if not 0 <= length <= MAX_BODY:
                self.send_json(413, {"error": "body_too_large"})
                return
            try:
                raw = self.rfile.read(length)
                if len(raw) != length:
                    raise ValueError("incomplete body")
                body = json.loads(raw) if raw else {}
                if not isinstance(body, dict):
                    raise ValueError("object required")
            except (ValueError, UnicodeError, OSError):
                self.send_json(400, {"error": "invalid_json"})
                return
            status, value = fixture.dispatch(self.command, self.path, self.headers.get("Authorization", ""), body)
            self.send_json(status, value)

        do_GET = do_POST = do_PUT = route
    return Handler


def mqtt_string(value: str) -> bytes:
    encoded = value.encode()
    return struct.pack("!H", len(encoded)) + encoded


def mqtt_packet(kind: int, payload: bytes) -> bytes:
    length = len(payload)
    encoded = bytearray([kind])
    while True:
        byte = length % 128
        length //= 128
        encoded.append(byte | (0x80 if length else 0))
        if not length:
            return bytes(encoded) + payload


def broker_publisher(config: dict, ca: Path, host: str):
    if host not in ("mosquitto", "127.0.0.1"):
        raise ValueError("Only the local Compose broker or loopback broker is supported")
    credentials = config["controller_mqtt"]
    context = ssl.create_default_context(cafile=str(ca))
    context.minimum_version = ssl.TLSVersion.TLSv1_2

    def publish(identity: str, message: dict):
        payload = mqtt_string("MQTT") + b"\x04\xc2\x00\x0a" + mqtt_string("local-fixture-controller")
        payload += mqtt_string(credentials["username"]) + mqtt_string(credentials["password"])
        port = 8883 if host == "mosquitto" else config.get("mqtt_port", 8883)
        with socket.create_connection((host, port), timeout=5) as plain:
            with context.wrap_socket(plain, server_hostname=host) as secure:
                secure.sendall(mqtt_packet(0x10, payload))
                reply = b""
                while len(reply) < 4:
                    chunk = secure.recv(4 - len(reply))
                    if not chunk:
                        raise OSError("Local broker closed connection")
                    reply += chunk
                if reply != b"\x20\x02\x00\x00":
                    raise OSError("Local broker rejected fixture controller")
                secure.sendall(mqtt_packet(0x30, mqtt_string(identity) + json.dumps(message).encode()))
                secure.sendall(b"\xe0\x00")
    return publish


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--cert", type=Path)
    parser.add_argument("--key", type=Path)
    parser.add_argument("--ca", type=Path, required=True)
    parser.add_argument("--broker", default="mosquitto", choices=("mosquitto", "127.0.0.1"))
    parser.add_argument("--healthcheck", action="store_true")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--bind", choices=("127.0.0.1", "0.0.0.0"), default="127.0.0.1")
    args = parser.parse_args()
    config = json.loads(args.config.read_text())
    if args.healthcheck:
        context = ssl.create_default_context(cafile=str(args.ca))
        request = urllib.request.Request(f"https://127.0.0.1:{args.port}/api/health-check", headers={"Authorization": "Bearer " + config["controller_api_token"]})
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), urllib.request.HTTPSHandler(context=context))
        with opener.open(request, timeout=2) as response:
            return 0 if response.status == 200 else 1
    if not args.cert or not args.key:
        parser.error("--cert and --key are required to start HTTPS")
    fixture = Fixture(config, broker_publisher(config, args.ca, args.broker))
    server = ThreadingHTTPServer((args.bind, args.port), handler_for(fixture))
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.cert, args.key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    server.serve_forever()


if __name__ == "__main__":
    raise SystemExit(main())
