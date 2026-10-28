#!/usr/bin/env python3
"""Exercise authorization, device isolation, and HTTPS using local fixtures only."""

import importlib.util
from email.utils import parsedate_to_datetime
import json
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

import http_fixture as fixture_module

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import dev_services

FIRST = "AA:BB:CC:DD:EE:FF"
SECOND = "AA:BB:CC:DD:EE:FE"


def synthetic_config():
    return {"controller_api_token": "synthetic-controller", "devices": {
        FIRST: {"api_token": "synthetic-first", "username": "first", "password": "first-pass", "pairing_code": "first-pair"},
        SECOND: {"api_token": "synthetic-second", "username": "second", "password": "second-pass", "pairing_code": "second-pair"}}}


class AuthorizationTests(unittest.TestCase):
    def setUp(self):
        self.published = []
        self.fixture = fixture_module.Fixture(synthetic_config(), lambda topic, message: self.published.append((topic, message)))

    def call(self, path, body=None, token="synthetic-first", method="POST"):
        return self.fixture.dispatch(method, path, "Bearer " + token, body or {})

    def test_missing_and_wrong_credentials_rejected(self):
        for token in ("", "wrong", "bearer synthetic-first", "Bearer café", "Bearer " + "x" * 129):
            for path in ("/api/health", "/api/health-check"):
                self.assertEqual(self.fixture.dispatch("GET", path, token, {})[0], 401)
        for path in ("/api/health", "/api/health-check"):
            self.assertEqual(self.call(path, method="GET")[0], 200)

    def test_authentication_cannot_issue_another_device_credentials(self):
        self.assertEqual(self.call("/api/lockbox/auth", {"mac": SECOND})[0], 403)
        code, body = self.call("/api/lockbox/auth", {"mac": FIRST})
        self.assertEqual(code, 200)
        self.assertEqual(body["clientId"], FIRST)
        self.assertEqual(body["password"], "first-pass")

    def test_device_state_and_pairing_are_isolated(self):
        self.assertEqual(self.call("/api/lockbox/state/" + SECOND, method="GET")[0], 403)
        self.assertEqual(self.call("/api/lockbox/state/" + SECOND, {"state": "locked"})[0], 403)
        self.assertEqual(self.call("/api/lockbox/test-pair", {"macAddress": SECOND})[0], 403)
        self.assertEqual(self.call("/api/lockbox/state/" + FIRST, {"lockState": "locked"})[0], 400)
        self.assertEqual(self.call("/api/lockbox/state/" + FIRST, {"state": "locked", "isLocked": True})[0], 200)
        self.assertEqual(self.fixture.sessions[FIRST]["lockState"], "unlocked")

    def test_pairing_response_matches_production_contract(self):
        code, body = self.call("/api/lockbox/is-paired", {"macAddress": FIRST})
        self.assertEqual(code, 200)
        self.assertEqual(body["macAddress"], FIRST)
        self.assertTrue(body["bubbleId"])
        self.fixture.paired[FIRST] = False
        self.assertEqual(self.call("/api/lockbox/is-paired", {"macAddress": FIRST})[0], 418)
        self.assertEqual(self.call("/api/lockbox/test-pair", {"macAddress": FIRST})[0], 200)
        self.assertEqual(self.call("/api/lockbox/is-paired", {"macAddress": FIRST})[0], 200)

    def test_controller_only_session_updates_publish_scoped_topic(self):
        path = "/api/local/v1/session/" + FIRST
        self.assertEqual(self.call(path, {"lockState": "locked"}, method="PUT")[0], 403)
        self.assertEqual(self.call(path, {"lockState": "locked", "lockEnd": 99999}, token="synthetic-controller", method="PUT")[0], 200)
        self.assertEqual(self.fixture.sessions[SECOND]["lockState"], "unlocked")
        self.assertEqual(self.published[0][0], FIRST)
        self.assertEqual(self.published[0][1]["type"], "sync")
        self.assertEqual(self.call(path, {"lockState": "locked", "lockEnd": True}, token="synthetic-controller", method="PUT")[0], 400)

    def test_http_duration_ms_matches_mqtt_duration_seconds(self):
        path = "/api/local/v1/session/" + FIRST
        code, response = self.call(path, {"lockState": "locked", "duration": 60000}, token="synthetic-controller", method="PUT")
        self.assertEqual(code, 200)
        self.assertEqual(response["data"]["duration"], 60000)
        self.assertEqual(self.published[-1][1]["duration"], 60)
        self.assertEqual(self.call("/api/lockbox/state/" + FIRST, method="GET")[1]["data"]["duration"], 60000)

    def test_firmware_fixture_never_returns_downloads_or_cloud_redirects(self):
        report = {"deviceType": "lkbx", "deviceId": FIRST, "reportedTrack": "main", "currentVersion": "1.2.3"}
        code, body = self.call("/api/firmware/v1/check", report)
        self.assertEqual(code, 200)
        self.assertFalse(body["shouldUpdate"])
        self.assertIsNone(body["update"])
        report["deviceId"] = SECOND
        self.assertEqual(self.call("/api/firmware/v1/check", report)[0], 403)
        self.assertEqual(self.call("https://dashboard.researchanddesire.com/api/health-check", method="GET")[0], 400)
        self.assertEqual(self.call("/api/health-check?url=https://example.com", method="GET")[0], 400)
        with self.assertRaises(ValueError):
            fixture_module.broker_publisher({}, Path("unused"), "public.example.com")


class HttpsProcessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory()
        cls.root = Path(cls.temporary.name)
        cls.config = dev_services.configure(cls.root, device_ids=[FIRST, SECOND])
        cls.folder = cls.root / ".dev/local"
        with socket.socket() as sock:
            sock.bind(("127.0.0.1", 0))
            cls.port = sock.getsockname()[1]
        cls.context = ssl.create_default_context(cafile=str(cls.folder / "ca.pem"))
        cls.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), urllib.request.HTTPSHandler(context=cls.context))
        cls.process = subprocess.Popen([sys.executable, "-B", str(ROOT / "fixture/http_fixture.py"), "--config", str(cls.folder / "settings.json"),
                                       "--ca", str(cls.folder / "ca.pem"), "--cert", str(cls.folder / "server.pem"), "--key", str(cls.folder / "server.key"),
                                       "--broker", "127.0.0.1", "--port", str(cls.port)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", cls.port), timeout=0.1):
                    return
            except OSError:
                time.sleep(0.02)
        cls.process.terminate()
        raise RuntimeError("Local HTTPS fixture failed to start")

    @classmethod
    def tearDownClass(cls):
        cls.process.terminate()
        cls.process.communicate(timeout=5)
        cls.temporary.cleanup()

    def request(self, path, token=None, raw=None):
        headers = {"Content-Type": "application/json"}
        if token:
            headers["Authorization"] = "Bearer " + token
        request = urllib.request.Request(f"https://127.0.0.1:{self.port}" + path, data=raw, headers=headers)
        try:
            with self.opener.open(request, timeout=2) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    def test_real_https_auth_and_device_scope(self):
        self.assertEqual(self.request("/api/health-check")[0], 401)
        token = self.config["devices"][FIRST]["api_token"]
        self.assertEqual(self.request("/api/health-check", token)[0], 200)
        self.assertEqual(self.request("/api/lockbox/auth", token, json.dumps({"mac": SECOND}).encode())[0], 403)
        self.assertEqual(self.request("/api/lockbox/auth", token, json.dumps({"mac": FIRST}).encode())[0], 200)

    def test_firmware_clock_endpoint_returns_authenticated_current_date(self):
        # The firmware clock bootstrap uses /api/health, while its connectivity
        # probe uses /api/health-check. Both must remain authenticated.
        self.assertEqual(self.request("/api/health")[0], 401)
        request = urllib.request.Request(
            f"https://127.0.0.1:{self.port}/api/health",
            headers={"Authorization": "Bearer " + self.config["devices"][FIRST]["api_token"]},
        )
        before = time.time()
        with self.opener.open(request, timeout=2) as response:
            self.assertEqual(response.status, 200)
            dates = response.headers.get_all("Date")
            self.assertEqual(len(dates), 1)
            self.assertEqual(len(dates[0]), 29)
            timestamp = parsedate_to_datetime(dates[0]).timestamp()
        self.assertGreaterEqual(timestamp, int(before) - 1)
        self.assertLessEqual(timestamp, time.time() + 1)

    def test_real_https_rejects_malformed_and_oversize_body(self):
        token = self.config["devices"][FIRST]["api_token"]
        self.assertEqual(self.request("/api/lockbox/auth", token, b"[]")[0], 400)
        self.assertEqual(self.request("/api/lockbox/auth", token, b"{" * 17000)[0], 413)


if __name__ == "__main__":
    unittest.main()
