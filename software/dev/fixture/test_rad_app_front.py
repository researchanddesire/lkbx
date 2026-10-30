#!/usr/bin/env python3
"""Exercise the RAD App TLS front against loopback stand-ins; no RAD App, Docker or cloud."""

import asyncio
import json
from pathlib import Path
import ssl
import sys
import tempfile
import unittest

import rad_app_front as front_module

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import dev_services


class FrontTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        # Certificate generation is synchronous; debug mode would report it as a slow callback.
        asyncio.get_running_loop().set_debug(False)
        self.directory = tempfile.TemporaryDirectory()
        dev_services.configure(Path(self.directory.name), "127.0.0.1")
        folder = Path(self.directory.name) / ".dev/local"
        self.client_context = ssl.create_default_context(cafile=str(folder / "ca.pem"))
        self.requests = []
        self.servers = []

        async def rad_app(reader, writer):
            # Read only the head plus declared body, as a real server would.
            head = await reader.readuntil(b"\r\n\r\n")
            length = next((int(line.split(b":")[1]) for line in head.split(b"\r\n")
                           if line.lower().startswith(b"content-length:")), 0)
            self.requests.append(head + await reader.readexactly(length))
            body = b'{"status":"ok"}'
            writer.write(b"HTTP/1.1 200 OK\r\nDate: Wed, 28 Oct 2026 18:25:13 GMT\r\nContent-Length: "
                         + str(len(body)).encode() + b"\r\n\r\n" + body)
            await writer.drain()
            writer.close()

        async def broker(reader, writer):
            while data := await reader.read(1024):
                writer.write(b"echo:" + data)
                await writer.drain()
            writer.close()

        app_server = await asyncio.start_server(rad_app, "127.0.0.1", 0)
        broker_server = await asyncio.start_server(broker, "127.0.0.1", 0)
        self.app_port = app_server.sockets[0].getsockname()[1]
        front = front_module.Front("127.0.0.1", self.app_port, broker_server.sockets[0].getsockname()[1])
        listeners = await front_module.serve(front, front_module.server_context(folder / "server.pem", folder / "server.key"),
                                             "127.0.0.1", 0, 0)
        self.api_port, self.mqtt_port = (server.sockets[0].getsockname()[1] for server in listeners)
        self.servers = [app_server, broker_server, *listeners]

    async def asyncTearDown(self):
        for server in self.servers:
            server.close()
        self.directory.cleanup()

    async def https(self, raw: bytes) -> bytes:
        reader, writer = await asyncio.open_connection("127.0.0.1", self.api_port, ssl=self.client_context,
                                                       server_hostname="127.0.0.1")
        writer.write(raw)
        await writer.drain()
        response = await asyncio.wait_for(reader.read(), 5)
        writer.close()
        return response

    async def test_forwards_one_request_without_fixture_credentials(self):
        body = b'{"id":"1"}'
        response = await self.https(b"POST /api/lockbox/state/AA:BB:CC:DD:EE:FF HTTP/1.1\r\nHost: 127.0.0.1:8443\r\n"
                                    b"Authorization: Bearer fixture-device-token\r\nConnection: keep-alive\r\n"
                                    b"Content-Type: application/json\r\nContent-Length: " + str(len(body)).encode() +
                                    b"\r\n\r\n" + body)
        self.assertTrue(response.startswith(b"HTTP/1.1 200 OK\r\n"))
        self.assertIn(b"Date: Wed, 28 Oct 2026 18:25:13 GMT", response)
        self.assertTrue(response.endswith(b'{"status":"ok"}'))
        [request] = self.requests
        self.assertTrue(request.startswith(b"POST /api/lockbox/state/AA:BB:CC:DD:EE:FF HTTP/1.1\r\n"))
        self.assertNotIn(b"fixture-device-token", request)
        self.assertNotIn(b"keep-alive", request.lower())
        self.assertIn(b"Connection: close\r\n", request)
        self.assertTrue(request.endswith(b"\r\n\r\n" + body))

    async def test_firmware_checks_never_reach_rad_app(self):
        report = json.dumps({"deviceType": "lkbx", "deviceId": "AA:BB:CC:DD:EE:FF", "reportedTrack": "main",
                             "currentVersion": "0.0.0"}).encode()
        request = (b"POST /api/firmware/v1/check HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: "
                   + str(len(report)).encode() + b"\r\n\r\n" + report)
        response = await self.https(request)
        self.assertTrue(response.startswith(b"HTTP/1.1 200 OK\r\n"))
        decision = json.loads(response.split(b"\r\n\r\n", 1)[1])
        self.assertFalse(decision["shouldUpdate"])
        self.assertIsNone(decision["update"])
        other = json.dumps({"deviceType": "dtt", "reportedTrack": "main", "currentVersion": "0.0.0"}).encode()
        self.assertIn(b" 403 ", await self.https(request.replace(report, other).replace(
            str(len(report)).encode(), str(len(other)).encode())))
        self.assertIn(b" 404 ", await self.https(b"GET /api/firmware/v1/web-flasher/catalog HTTP/1.1\r\n\r\n"))
        self.assertEqual(self.requests, [])

    async def test_ambiguous_requests_are_refused_before_forwarding(self):
        for raw in (b"GET /api/%66irmware/v1/check HTTP/1.1\r\n\r\n",
                    b"GET /api/lockbox/../firmware/v1/check HTTP/1.1\r\n\r\n",
                    b"GET //api/firmware/v1/check HTTP/1.1\r\n\r\n",
                    b"POST /api/lockbox/auth HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
                    b"POST /api/lockbox/auth HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
                    b"POST /api/lockbox/auth HTTP/1.1\r\nContent-Length: 99999\r\n\r\n",
                    b"GET /api/health-check HTTP/1.1\r\nX-Bad: one\x00two\r\n\r\n",
                    b"GET http://example.com/ HTTP/1.1\r\n\r\n"):
            with self.subTest(raw=raw):
                response = await self.https(raw)
                self.assertRegex(response, rb"^HTTP/1\.1 (400|413) ")
        self.assertEqual(self.requests, [])

    async def test_missing_rad_app_is_a_local_gateway_error(self):
        self.servers[0].close()
        await self.servers[0].wait_closed()
        response = await self.https(b"GET /api/health-check HTTP/1.1\r\n\r\n")
        self.assertTrue(response.startswith(b"HTTP/1.1 502 "))

    async def test_mqtt_is_relayed_both_ways_over_verified_tls(self):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.mqtt_port, ssl=self.client_context,
                                                       server_hostname="127.0.0.1")
        writer.write(b"\x10\x00")
        await writer.drain()
        self.assertEqual(await asyncio.wait_for(reader.readexactly(7), 5), b"echo:\x10\x00")
        writer.close()
        untrusted = ssl.create_default_context()
        # The front's listener reports the refused handshake; that is expected here.
        asyncio.get_running_loop().set_exception_handler(lambda loop, context: None)
        with self.assertRaises(ssl.SSLError):
            await asyncio.open_connection("127.0.0.1", self.mqtt_port, ssl=untrusted, server_hostname="127.0.0.1")


if __name__ == "__main__":
    unittest.main()
