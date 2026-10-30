#!/usr/bin/env python3
"""Local TLS front for a running RAD App and its EMQX broker; no cloud fallback.

Development firmware trusts only the local CA and the HTTPS/MQTTS endpoints that
dev_services.py generates. RAD App serves plain HTTP on :3000 and EMQX plain
MQTT on :1883, so this front terminates TLS with the fixture certificate and
forwards to those two ports on one local upstream host only. Firmware checks are
answered here, exactly as the fixture answers them, so a local dashboard can
never hand a development device a cloud-hosted artifact.
"""

from __future__ import annotations

import argparse
import asyncio
from email.utils import formatdate
import json
from pathlib import Path
import ssl
import urllib.request

from http_fixture import MAX_BODY, no_update

MAX_HEAD = 16384
MAX_CONNECTIONS = 32
REQUEST_SECONDS = 60
# Hop-by-hop headers never pass, so each connection carries exactly one
# inspected request. Production firmware sends no Authorization; the fixture
# bearer that development firmware adds stays out of RAD App.
DROPPED_HEADERS = {"authorization", "connection", "expect", "keep-alive", "proxy-authorization",
                   "proxy-connection", "te", "trailer", "upgrade"}
TOKEN = set("!#$%&'*+-.^_`|~0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz")
REASONS = {200: "OK", 400: "Bad Request", 403: "Forbidden", 404: "Not Found", 413: "Payload Too Large",
           502: "Bad Gateway"}


def target_path(target: str) -> str:
    # Not urlsplit: it reads "//api/..." as a host and would hide the real path.
    return target.partition("?")[0]


class BadRequest(Exception):
    def __init__(self, status: int, error: str):
        super().__init__(error)
        self.status = status
        self.error = error


def json_response(status: int, value: dict) -> bytes:
    body = json.dumps(value, separators=(",", ":")).encode()
    head = (f"HTTP/1.1 {status} {REASONS[status]}\r\nDate: {formatdate(usegmt=True)}\r\n"
            f"Content-Type: application/json\r\nContent-Length: {len(body)}\r\n"
            "Cache-Control: no-store\r\nConnection: close\r\n\r\n")
    return head.encode() + body


def parse_head(head: bytes) -> tuple[str, str, str, list[tuple[str, str]], int]:
    """Parse one request head (without its blank line) strictly enough to re-serialize it."""
    try:
        lines = head.decode("ascii").split("\r\n")
    except UnicodeDecodeError as error:
        raise BadRequest(400, "invalid_head") from error
    parts = lines[0].split(" ")
    if (len(parts) != 3 or not parts[0] or not set(parts[0]) <= TOKEN or
            parts[2] not in ("HTTP/1.0", "HTTP/1.1") or not parts[1].startswith("/") or
            any(not 0x21 <= ord(c) < 0x7f for c in parts[1])):
        raise BadRequest(400, "invalid_request_line")
    path = target_path(parts[1])
    # Only canonical paths, so the firmware-route check below cannot be bypassed
    # by encoding, empty or dot segments that the upstream router would normalize.
    if (any(c in path for c in "%\\") or "//" in path or "#" in parts[1] or
            any(s in (".", "..") for s in path.split("/"))):
        raise BadRequest(400, "invalid_path")
    headers = []
    for line in lines[1:]:
        name, separator, value = line.partition(":")
        if (not separator or not name or not set(name) <= TOKEN or
                any(ord(c) < 0x20 and c != "\t" or ord(c) == 0x7f for c in value)):
            raise BadRequest(400, "invalid_header")
        headers.append((name, value.strip()))
    if any(name.lower() == "transfer-encoding" for name, _ in headers):
        raise BadRequest(400, "unsupported_encoding")
    lengths = [value for name, value in headers if name.lower() == "content-length"]
    if len(lengths) > 1 or (lengths and not lengths[0].isdigit()):
        raise BadRequest(400, "invalid_length")
    length = int(lengths[0]) if lengths else 0
    if length > MAX_BODY:
        raise BadRequest(413, "body_too_large")
    return parts[0], parts[1], parts[2], headers, length


def firmware_route(method: str, target: str, body: bytes) -> tuple[int, dict]:
    if target_path(target).lower() != "/api/firmware/v1/check" or method != "POST":
        return 404, {"error": "not_found"}
    try:
        report = json.loads(body)
    except ValueError:
        return 400, {"error": "invalid_json"}
    if not isinstance(report, dict) or report.get("deviceType") != "lkbx":
        return 403, {"error": "device_denied"}
    return no_update(report)


async def pipe(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    try:
        while data := await reader.read(65536):
            writer.write(data)
            await writer.drain()
    finally:
        writer.close()


class Front:
    def __init__(self, upstream: str, app_port: int = 3000, broker_port: int = 1883):
        self.upstream = upstream
        self.app_port = app_port
        self.broker_port = broker_port
        self.active = 0

    async def guarded(self, handler, reader, writer):
        if self.active >= MAX_CONNECTIONS:
            writer.close()
            return
        self.active += 1
        try:
            await handler(reader, writer)
        except (OSError, EOFError, ValueError, asyncio.TimeoutError, ssl.SSLError):
            # Peers that disconnect, time out or fail TLS only lose their own connection.
            pass
        finally:
            self.active -= 1
            writer.close()

    async def http(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        try:
            head = await asyncio.wait_for(reader.readuntil(b"\r\n\r\n"), 10)
            method, target, version, headers, length = parse_head(head[:-4])
            body = await asyncio.wait_for(reader.readexactly(length), 10)
        except asyncio.LimitOverrunError:
            writer.write(json_response(400, {"error": "head_too_large"}))
            return await writer.drain()
        except BadRequest as error:
            writer.write(json_response(error.status, {"error": error.error}))
            return await writer.drain()
        if target_path(target).lower().startswith("/api/firmware/"):
            writer.write(json_response(*firmware_route(method, target, body)))
            return await writer.drain()
        try:
            upstream_reader, upstream_writer = await asyncio.wait_for(
                asyncio.open_connection(self.upstream, self.app_port), 5)
        except (OSError, asyncio.TimeoutError):
            writer.write(json_response(502, {"error": "rad_app_unavailable"}))
            return await writer.drain()
        try:
            kept = "".join(f"{name}: {value}\r\n" for name, value in headers if name.lower() not in DROPPED_HEADERS)
            upstream_writer.write(f"{method} {target} {version}\r\n{kept}Connection: close\r\n\r\n".encode() + body)
            await upstream_writer.drain()
            await asyncio.wait_for(pipe(upstream_reader, writer), REQUEST_SECONDS)
        finally:
            upstream_writer.close()

    async def mqtt(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        upstream_reader, upstream_writer = await asyncio.wait_for(
            asyncio.open_connection(self.upstream, self.broker_port), 5)
        # Closing either side ends the other, so neither direction outlives its peer.
        await asyncio.gather(pipe(reader, upstream_writer), pipe(upstream_reader, writer), return_exceptions=True)


def server_context(cert: Path, key: Path) -> ssl.SSLContext:
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert, key)
    return context


async def serve(front: Front, context: ssl.SSLContext, bind: str, api_port: int = 8443, mqtt_port: int = 8883):
    servers = []
    for handler, port in ((front.http, api_port), (front.mqtt, mqtt_port)):
        servers.append(await asyncio.start_server(
            lambda reader, writer, handler=handler: front.guarded(handler, reader, writer),
            bind, port, ssl=context, ssl_handshake_timeout=10, limit=MAX_HEAD))
    return servers


def healthcheck(ca: Path, port: int = 8443) -> int:
    # Healthy means the verified HTTPS listener reaches RAD App end to end.
    context = ssl.create_default_context(cafile=str(ca))
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), urllib.request.HTTPSHandler(context=context))
    with opener.open(f"https://127.0.0.1:{port}/api/health-check", timeout=4) as response:
        return 0 if response.status == 200 else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cert", type=Path)
    parser.add_argument("--key", type=Path)
    parser.add_argument("--ca", type=Path, required=True)
    parser.add_argument("--upstream", default="host.docker.internal", choices=("host.docker.internal", "127.0.0.1"))
    parser.add_argument("--bind", choices=("127.0.0.1", "0.0.0.0"), default="127.0.0.1")
    parser.add_argument("--healthcheck", action="store_true")
    args = parser.parse_args()
    if args.healthcheck:
        return healthcheck(args.ca)
    if not args.cert or not args.key:
        parser.error("--cert and --key are required to start the TLS front")

    async def run():
        await serve(Front(args.upstream), server_context(args.cert, args.key), args.bind)
        await asyncio.Event().wait()

    asyncio.run(run())


if __name__ == "__main__":
    raise SystemExit(main())
