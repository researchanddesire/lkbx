#!/usr/bin/env python3
"""Opt-in tests against loopback-only Compose fixtures; no physical devices or cloud."""

import json
from pathlib import Path
import secrets
import socket
import ssl
import struct
import unittest
import urllib.error
import urllib.request

from http_fixture import mqtt_packet, mqtt_string

ROOT = Path(__file__).resolve().parents[1]
FIRST = "AA:BB:CC:DD:EE:FF"
SECOND = "AA:BB:CC:DD:EE:FE"


class Client:
    def __init__(self, context, username=None, password=None, client_id=None):
        plain = socket.create_connection(("127.0.0.1", 8883), timeout=3)
        self.sock = context.wrap_socket(plain, server_hostname="127.0.0.1")
        flags = 0xC2 if username is not None else 0x02
        body = mqtt_string("MQTT") + bytes([4, flags, 0, 10]) + mqtt_string(client_id or "fixture-test-" + secrets.token_hex(4))
        if username is not None:
            body += mqtt_string(username) + mqtt_string(password)
        self.sock.sendall(mqtt_packet(0x10, body))
        self.accepted = self.read() == (0x20, b"\x00\x00")

    def close(self):
        self.sock.close()

    def exact(self, count):
        data = b""
        while len(data) < count:
            chunk = self.sock.recv(count - len(data))
            if not chunk:
                raise OSError("Local broker disconnected")
            data += chunk
        return data

    def read(self, timeout=3):
        self.sock.settimeout(timeout)
        kind = self.exact(1)[0]
        length = 0
        multiplier = 1
        for _ in range(4):
            byte = self.exact(1)[0]
            length += (byte & 127) * multiplier
            if not byte & 128:
                return kind, self.exact(length)
            multiplier *= 128
        raise ValueError("Invalid MQTT length")

    def subscribe(self, topic):
        self.sock.sendall(mqtt_packet(0x82, b"\x00\x01" + mqtt_string(topic) + b"\x00"))
        kind, _ = self.read()
        if kind != 0x90:
            raise ValueError("Missing local broker subscription response")

    def publish(self, topic, body):
        self.sock.sendall(mqtt_packet(0x30, mqtt_string(topic) + body))

    def message(self, timeout=3):
        kind, packet = self.read(timeout)
        if kind & 0xF0 != 0x30:
            raise ValueError("Expected a local fixture publication")
        length = struct.unpack("!H", packet[:2])[0]
        return packet[2:2 + length].decode(), packet[2 + length:]


class ComposeIntegrationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.config = json.loads((ROOT / ".dev/local/settings.json").read_text())
        if cls.config["lan_ip"] != "127.0.0.1" or not all(identity in cls.config["devices"] for identity in (FIRST, SECOND)):
            raise RuntimeError("Configure loopback fixtures with both synthetic IDs before these opt-in tests")
        cls.context = ssl.create_default_context(cafile=str(ROOT / ".dev/local/ca.pem"))
        cls.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), urllib.request.HTTPSHandler(context=cls.context))

    def connect(self, identity):
        credentials = self.config["devices"][identity]
        client = Client(self.context, credentials["username"], credentials["password"])
        self.addCleanup(client.close)
        self.assertTrue(client.accepted)
        return client

    def request(self, method, path, token=None, body=None):
        headers = {"Content-Type": "application/json"}
        if token:
            headers["Authorization"] = "Bearer " + token
        request = urllib.request.Request("https://127.0.0.1:8443" + path, method=method, headers=headers,
                                         data=json.dumps(body).encode() if body is not None else None)
        try:
            with self.opener.open(request, timeout=3) as response:
                return response.status, json.load(response)
        except urllib.error.HTTPError as error:
            return error.code, json.load(error)

    def test_https_and_auth_contract(self):
        token = self.config["devices"][FIRST]["api_token"]
        self.assertEqual(self.request("GET", "/api/health-check")[0], 401)
        self.assertEqual(self.request("GET", "/api/health-check", token)[0], 200)
        self.assertEqual(self.request("POST", "/api/lockbox/auth", token, {"mac": SECOND})[0], 403)
        code, body = self.request("POST", "/api/lockbox/auth", token, {"mac": FIRST})
        self.assertEqual(code, 200)
        self.assertEqual(body["clientId"], FIRST)
        self.assertEqual(body["password"], self.config["devices"][FIRST]["password"])
        self.assertEqual(self.request("POST", "/api/lockbox/is-paired", token, {"macAddress": FIRST})[1]["macAddress"], FIRST)

    def test_anonymous_and_wrong_broker_credentials_are_rejected(self):
        for username, password in ((None, None), (self.config["devices"][FIRST]["username"], "incorrect")):
            client = Client(self.context, username, password)
            self.addCleanup(client.close)
            self.assertFalse(client.accepted)

    def test_untrusted_certificate_and_wrong_hostname_are_rejected(self):
        for context, hostname in ((ssl.create_default_context(), "127.0.0.1"),
                                  (self.context, "wrong.fixture.invalid")):
            with socket.create_connection(("127.0.0.1", 8883), timeout=3) as plain:
                with self.assertRaises(ssl.SSLCertVerificationError):
                    context.wrap_socket(plain, server_hostname=hostname)

    def test_guessed_client_id_cannot_evict_another_device(self):
        second = self.connect(SECOND)
        second.subscribe(SECOND)
        first_credentials = self.config["devices"][FIRST]
        other_id = self.config["devices"][SECOND]["username"]
        first = Client(self.context, first_credentials["username"],
                       first_credentials["password"], client_id=other_id)
        self.addCleanup(first.close)
        self.assertTrue(first.accepted)
        marker = secrets.token_hex(8).encode()
        second.publish(SECOND, marker)
        self.assertEqual(second.message(), (SECOND, marker))

    def test_wildcard_subscription_cannot_disclose_another_device(self):
        first = self.connect(FIRST)
        second = self.connect(SECOND)
        first.subscribe("#")
        second.publish(SECOND, b"synthetic-private-state")
        with self.assertRaises(socket.timeout):
            first.message(timeout=0.5)

    def test_device_acl_allows_own_topic_and_denies_other_device_write(self):
        first = self.connect(FIRST)
        second = self.connect(SECOND)
        first.subscribe(FIRST)
        second.subscribe(SECOND)
        marker = secrets.token_hex(8).encode()
        first.publish(FIRST, marker)
        self.assertEqual(first.message(), (FIRST, marker))
        first.publish(SECOND, marker)
        with self.assertRaises(socket.timeout):
            second.message(timeout=0.5)
        second.publish(SECOND, marker)
        self.assertEqual(second.message(), (SECOND, marker))

    def test_device_acl_denies_other_device_read(self):
        first = self.connect(FIRST)
        second = self.connect(SECOND)
        first.subscribe(SECOND)
        second.publish(SECOND, b"synthetic-private-device-state")
        with self.assertRaises(socket.timeout):
            first.message(timeout=0.5)

    def test_only_controller_can_publish_session_changes(self):
        second = self.connect(SECOND)
        second.subscribe(SECOND)
        path = "/api/local/v1/session/" + SECOND
        body = {"lockState": "locked", "lockEnd": 999999}
        token = self.config["devices"][FIRST]["api_token"]
        self.assertEqual(self.request("PUT", path, token, body)[0], 403)
        self.assertEqual(self.request("PUT", path, self.config["controller_api_token"], body)[0], 200)
        topic, payload = second.message()
        self.assertEqual(topic, SECOND)
        self.assertEqual(json.loads(payload)["lockState"], "locked")
        self.assertEqual(self.request("GET", "/api/lockbox/state/" + FIRST, token)[1]["data"]["lockState"], "unlocked")


if __name__ == "__main__":
    unittest.main()
