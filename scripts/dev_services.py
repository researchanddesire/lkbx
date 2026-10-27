#!/usr/bin/env python3
"""Configure and operate isolated local lockbox fixtures; no cloud fallback."""

from __future__ import annotations

import argparse
import contextlib
import csv
from functools import lru_cache
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlsplit

DEFAULT_DEVICE = "AA:BB:CC:DD:EE:FF"
SCHEMA = 1
CERTIFICATE_PROFILE = 2
PRIVATE_NETWORKS = tuple(ipaddress.ip_network(x) for x in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "127.0.0.0/8"))


def local_address(value: str) -> str:
    try:
        address = ipaddress.ip_address(value)
    except ValueError as error:
        raise ValueError("Use an explicit private LAN IPv4 address or 127.0.0.1") from error
    if address.version != 4 or not any(address in block for block in PRIVATE_NETWORKS):
        raise ValueError("Only private LAN or loopback IPv4 addresses are allowed")
    if str(address).endswith(".255") or address.is_unspecified or address.is_multicast:
        raise ValueError("A unicast local IPv4 address is required")
    return str(address)


def device_id(value: str) -> str:
    value = value.upper()
    if not re.fullmatch(r"(?:[0-9A-F]{2}:){5}[0-9A-F]{2}", value):
        raise ValueError("Device ID must be a colon-separated MAC address")
    return value


def local_port(value: int) -> int:
    if type(value) is not int or not 1 <= value <= 65535:
        raise ValueError("Local service ports must be integers from 1 to 65535")
    return value


def local_dashboard_origin(value: str) -> str:
    parsed = urlsplit(value)
    if (parsed.scheme not in {"http", "https"} or not parsed.hostname or
            parsed.username is not None or parsed.password is not None or
            parsed.query or parsed.fragment or parsed.path not in {"", "/"}):
        raise ValueError("Dashboard origin must be an explicit local HTTP(S) origin")
    address = local_address(parsed.hostname)
    port = local_port(parsed.port) if parsed.port is not None else None
    return f"{parsed.scheme}://{address}" + (f":{port}" if port else "")


@lru_cache(maxsize=1)
def windows_user_sid() -> str:
    result = subprocess.run(["whoami", "/user", "/fo", "csv", "/nh"], stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, text=True, check=True)
    rows = list(csv.reader(result.stdout.splitlines()))
    sid = rows[0][1] if len(rows) == 1 and len(rows[0]) == 2 else ""
    if not re.fullmatch(r"S-1-5-\d+(?:-\d+)+", sid):
        raise RuntimeError("Could not establish the current Windows identity for private configuration")
    return sid


def private_directory(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    if os.name == "nt":
        # chmod on Windows changes only the read-only flag. Explicitly remove
        # inherited access before writing private tokens or TLS keys.
        result = subprocess.run(["icacls", str(directory), "/inheritance:r", "/grant:r",
                                 "*" + windows_user_sid() + ":(OI)(CI)F", "*S-1-5-18:(OI)(CI)F"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
        if result.returncode:
            raise RuntimeError("Could not restrict access to private local configuration")
    else:
        directory.chmod(0o700)


def write_private(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_file() and path.read_text() == contents:
        path.chmod(0o600)
        return
    with tempfile.NamedTemporaryFile(mode="w", dir=path.parent, delete=False, encoding="utf-8") as stream:
        temporary = Path(stream.name)
        os.chmod(temporary, 0o600)
        stream.write(contents)
    temporary.replace(path)


def openssl(*args: str) -> None:
    executable = shutil.which("openssl")
    if not executable:
        raise RuntimeError("OpenSSL is required to generate the local TLS certificates")
    result = subprocess.run([executable, *args], stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if result.returncode:
        raise RuntimeError("Local TLS certificate generation failed; verify the OpenSSL installation")


def certificate_files(directory: Path, address: str, preserve_leaf: bool = False) -> None:
    # The CA stays private to this checkout. Nothing is installed in a system trust store.
    if not (directory / "ca.key").exists() or not (directory / "ca.pem").exists():
        openssl("req", "-x509", "-newkey", "rsa:2048", "-sha256", "-nodes", "-days", "3650",
                "-subj", "/CN=Lockbox Local Development CA", "-keyout", str(directory / "ca.key"),
                "-out", str(directory / "ca.pem"), "-addext", "basicConstraints=critical,CA:TRUE",
                "-addext", "keyUsage=critical,keyCertSign,cRLSign")
    extensions = directory / "server.ext"
    write_private(extensions, "basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\n"
                  # The pinned ESP-IDF 4.4 mbedTLS verifier checks dNSName
                  # entries only, even for a literal-IP endpoint. Retain proper
                  # IP SANs for modern clients and add exact local names for
                  # that verifier; CA and hostname validation stay enabled.
                  f"subjectAltName=IP:{address},IP:127.0.0.1,DNS:{address},DNS:127.0.0.1,DNS:localhost,DNS:mosquitto\n")
    if preserve_leaf:
        # Repair existing names without moving notBefore ahead of a device's
        # cached clock. This keeps the existing key, CA and validity interval.
        replacement = directory / "server.replacement.pem"
        openssl("x509", "-in", str(directory / "server.pem"), "-CA", str(directory / "ca.pem"),
                "-CAkey", str(directory / "ca.key"), "-set_serial", str(secrets.randbits(128)),
                "-out", str(replacement), "-preserve_dates", "-sha256", "-extfile", str(extensions))
        replacement.chmod(0o600)
        replacement.replace(directory / "server.pem")
        return
    openssl("req", "-new", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=Lockbox Local Fixtures",
            "-keyout", str(directory / "server.key"), "-out", str(directory / "server.csr"))
    openssl("x509", "-req", "-in", str(directory / "server.csr"), "-CA", str(directory / "ca.pem"),
            "-CAkey", str(directory / "ca.key"), "-set_serial", str(secrets.randbits(128)),
            "-out", str(directory / "server.pem"), "-days", "365", "-sha256", "-extfile", str(extensions))
    for name in ("ca.key", "ca.pem", "server.key", "server.pem", "server.csr", "server.ext"):
        (directory / name).chmod(0o600)


@contextlib.contextmanager
def configuration_lock(directory: Path):
    path = directory / "configure.lock"
    deadline = time.monotonic() + 20
    while True:
        try:
            descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
            os.close(descriptor)
            break
        except FileExistsError:
            if time.monotonic() >= deadline:
                raise RuntimeError("Local configuration is locked; wait for the other build, or remove a stale .dev/local/configure.lock after confirming no build is running")
            time.sleep(0.05)
    try:
        yield
    finally:
        path.unlink()


def configure(root: Path, lan_ip: str | None = None, device_ids: list[str] | None = None,
              api_port: int | None = None, mqtt_port: int | None = None,
              dashboard_origin: str | None = None) -> dict:
    directory = Path(root).resolve() / ".dev/local"
    private_directory(directory)
    with configuration_lock(directory):
        return _configure_locked(root, lan_ip, device_ids, api_port, mqtt_port, dashboard_origin)


def _configure_locked(root: Path, lan_ip: str | None, device_ids: list[str] | None,
                      api_port: int | None, mqtt_port: int | None,
                      dashboard_origin: str | None) -> dict:
    root = Path(root).resolve()
    directory = root / ".dev/local"
    directory.mkdir(parents=True, exist_ok=True)
    directory.chmod(0o700)
    settings_path = directory / "settings.json"
    existed = settings_path.exists()
    settings = json.loads(settings_path.read_text()) if existed else {}
    if settings and settings.get("schema") != SCHEMA:
        raise ValueError("Unsupported local configuration version; preserve it and configure a new checkout")
    address = local_address(lan_ip or settings.get("lan_ip", "127.0.0.1"))
    existing = list(settings.get("devices", {}))
    primary = settings.get("primary_device")
    if primary in existing:
        existing = [primary] + [identity for identity in existing if identity != primary]
    selected = [device_id(value) for value in (device_ids or existing or [DEFAULT_DEVICE])]
    selected = list(dict.fromkeys(selected))
    old_address = settings.get("lan_ip")
    settings.setdefault("schema", SCHEMA)
    settings.setdefault("controller_api_token", secrets.token_urlsafe(32))
    settings.setdefault("controller_mqtt", {"username": "local-controller", "password": secrets.token_urlsafe(32)})
    devices = settings.setdefault("devices", {})
    # Existing identities are retained so a reconfiguration does not silently revoke another fixture.
    for identity in selected:
        devices.setdefault(identity, {"api_token": secrets.token_urlsafe(32), "username": "device-" + identity.replace(":", "").lower(),
                                      "password": secrets.token_urlsafe(32), "pairing_code": secrets.token_hex(3).upper()})
    settings["primary_device"] = selected[0]
    settings["lan_ip"] = address
    settings["api_port"] = local_port(api_port if api_port is not None else settings.get("api_port", 8443))
    settings["mqtt_port"] = local_port(mqtt_port if mqtt_port is not None else settings.get("mqtt_port", 8883))
    existing_dashboard = settings.get("dashboard_origin")
    if existing_dashboard == f"http://{old_address}:3000":
        existing_dashboard = None
    settings["dashboard_origin"] = local_dashboard_origin(dashboard_origin or existing_dashboard or f"http://{address}:3000")
    complete_certificates = all((directory / name).exists() for name in ("ca.key", "ca.pem", "server.key", "server.pem"))
    if old_address != address or settings.get("certificate_profile") != CERTIFICATE_PROFILE or not complete_certificates:
        certificate_files(directory, address, preserve_leaf=old_address == address and complete_certificates)
    settings["certificate_profile"] = CERTIFICATE_PROFILE
    write_private(settings_path, json.dumps(settings, indent=2) + "\n")
    passwords = []
    acl = []
    for identity, credentials in devices.items():
        passwords.append(credentials["username"] + ":" + credentials["password"])
        acl.extend(["user " + credentials["username"], f"topic readwrite {identity}", f"topic write {identity}/log", f"topic write {identity}/firmware", ""])
    controller = settings["controller_mqtt"]
    passwords.append(controller["username"] + ":" + controller["password"])
    acl.append("user " + controller["username"])
    for identity in devices:
        acl.extend([f"topic readwrite {identity}", f"topic read {identity}/log", f"topic read {identity}/firmware"])
    write_private(directory / "broker-passwords", "\n".join(passwords) + "\n")
    write_private(directory / "broker.acl", "\n".join(acl) + "\n")
    write_private(directory / "compose.env", f"LKBX_DEV_BIND_IP={address}\nLKBX_DEV_API_PORT={settings['api_port']}\nLKBX_DEV_MQTT_PORT={settings['mqtt_port']}\n")
    primary = devices[settings["primary_device"]]
    macros = {"LKBX_DEV_CA_PEM": (directory / "ca.pem").read_text(), "LKBX_DEV_API_TOKEN": primary["api_token"],
              "LKBX_DEV_API_BASE_URL": f"https://{address}:{settings['api_port']}", "LKBX_DEV_MQTT_URI": f"mqtts://{address}",
              "LKBX_DEV_DEVICE_ID": settings["primary_device"],
              "LKBX_DEV_DASHBOARD_ORIGIN": settings["dashboard_origin"]}
    header = "#pragma once\n#ifndef VERSIONDEV\n#error Local development configuration cannot be used in production\n#endif\n"
    header += "#if defined(RAD_SERVER) || defined(MQTT_SERVER) || defined(MQTT_PORT)\n#error Development endpoints must come from local fixture configuration\n#endif\n"
    header += "\n".join(f"#define {key} {json.dumps(value)}" for key, value in macros.items()) + "\n"
    header += f"#define LKBX_DEV_MQTT_PORT {settings['mqtt_port']}\n#define RAD_SERVER LKBX_DEV_API_BASE_URL\n#define RAD_SERVER_SHORT LKBX_DEV_API_BASE_URL\n#define MQTT_SERVER LKBX_DEV_MQTT_URI\n#define MQTT_PORT LKBX_DEV_MQTT_PORT\n"
    write_private(directory / "include/lkbx_dev_config.h", header)
    return settings


def should_start_services(profile: str, targets: set[str], ci: bool = False) -> bool:
    return not ci and profile in {"development-r2", "development-r8"} and bool(targets & {"upload", "test", "__test"})


def docker_environment(root: Path, executable: str) -> dict[str, str]:
    # These fixtures use public images only. Do not inherit account credentials or
    # invoke desktop credential helpers when operating the project-local stack.
    environment = os.environ.copy()
    endpoint = environment.get("DOCKER_HOST")
    if not endpoint:
        result = subprocess.run([executable, "context", "inspect", "--format", "{{.Endpoints.docker.Host}}"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=10, check=False)
        if result.returncode:
            raise RuntimeError("Could not resolve the local Docker daemon; cloud fallback is disabled")
        endpoint = result.stdout.strip()
    if not endpoint.startswith(("unix://", "npipe://")):
        raise RuntimeError("Local fixture operations require a local Docker socket; remote Docker endpoints are refused")
    original_config = Path(environment.get("DOCKER_CONFIG", str(Path.home() / ".docker")))
    directory = root / ".dev/docker-client"
    private_directory(directory)
    plugin_dir = original_config / "cli-plugins"
    configuration = {"auths": {}}
    if plugin_dir.is_dir():
        configuration["cliPluginsExtraDirs"] = [str(plugin_dir)]
    write_private(directory / "config.json", json.dumps(configuration) + "\n")
    environment["DOCKER_CONFIG"] = str(directory)
    environment["DOCKER_HOST"] = endpoint
    environment.pop("DOCKER_CONTEXT", None)
    return environment


def compose(root: Path, action: str) -> None:
    root = Path(root).resolve()
    config = root / ".dev/local/compose.env"
    if not config.exists():
        raise RuntimeError("Local fixtures are not configured; run configure first")
    executable = shutil.which("docker")
    if not executable:
        raise RuntimeError("Docker with Compose is required to run local fixtures")
    command = [executable, "compose", "--project-name", "lkbx-local-" + __import__("hashlib").sha256(str(root).encode()).hexdigest()[:10],
               "--env-file", str(config), "-f", str(root / "compose.yaml")]
    command += {"up": ["up", "--detach", "--force-recreate", "--wait", "--wait-timeout", "45"], "down": ["down"], "status": ["ps"]}[action]
    result = subprocess.run(command, cwd=root, env=docker_environment(root, executable), check=False)
    if result.returncode:
        raise RuntimeError("Local Docker fixtures failed; start the Docker daemon and retry. Cloud fallback is disabled.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, epilog="Configure --lan-ip with the private address reachable by your device before uploading. Configuration and TLS keys stay in ignored .dev/local; nothing is installed globally. Native/compile-only builds do not start Docker.")
    parser.add_argument("action", choices=("configure", "up", "down", "status"))
    parser.add_argument("--lan-ip", help="Private LAN IPv4 address; defaults to existing configuration or loopback")
    parser.add_argument("--device-id", action="append", dest="device_ids", help="Synthetic device MAC (repeat for independent devices); first identity is used by the development firmware")
    parser.add_argument("--api-port", type=int, help="Published local HTTPS port (default 8443)")
    parser.add_argument("--mqtt-port", type=int, help="Published local MQTTS port (default 8883)")
    parser.add_argument("--dashboard-origin", help="Local web UI origin for pairing QR (default http://<lan-ip>:3000)")
    parser.add_argument("--project-dir", type=Path, default=Path(__file__).resolve().parents[1], help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    try:
        if args.action in {"configure", "up"}:
            settings = configure(args.project_dir, args.lan_ip, args.device_ids, args.api_port, args.mqtt_port, args.dashboard_origin)
            print(f"Local HTTPS: https://{settings['lan_ip']}:{settings['api_port']}; MQTTS: {settings['lan_ip']}:{settings['mqtt_port']}; device: {settings['primary_device']}")
        if args.action != "configure":
            compose(args.project_dir, args.action)
        return 0
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError, subprocess.TimeoutExpired) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
