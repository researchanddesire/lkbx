#!/usr/bin/env python3
"""Local fixture configuration tests; never require Docker or a physical device."""

import configparser
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import ssl
import stat
import tempfile
import unittest
from unittest.mock import patch

import dev_services as local

ROOT = Path(__file__).resolve().parents[1]


class ConfigurationTests(unittest.TestCase):
    def test_addresses_cannot_select_cloud_or_dns(self):
        for value in ("8.8.8.8", "dashboard.researchanddesire.com", "0.0.0.0", "169.254.169.254", "100.64.0.1", "::1", "224.0.0.1", "192.168.1.255"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                local.local_address(value)
        self.assertEqual(local.local_address("192.168.1.17"), "192.168.1.17")

    def test_configuration_is_private_idempotent_and_scoped(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = local.configure(root, "127.0.0.1", [local.DEFAULT_DEVICE, "AA:BB:CC:DD:EE:FE"])
            folder = root / ".dev/local"
            first_cert = (folder / "server.pem").read_bytes()
            header_time = (folder / "include/lkbx_dev_config.h").stat().st_mtime_ns
            second = local.configure(root)
            self.assertEqual(first, second)
            self.assertEqual(first_cert, (folder / "server.pem").read_bytes())
            self.assertEqual(header_time, (folder / "include/lkbx_dev_config.h").stat().st_mtime_ns)
            self.assertNotEqual(first["devices"][local.DEFAULT_DEVICE]["api_token"], first["devices"]["AA:BB:CC:DD:EE:FE"]["api_token"])
            if os.name == "nt":
                permissions = local.subprocess.check_output(["icacls", str(folder)], text=True)
                self.assertNotIn("(I)", permissions)
                for identity in ("Everyone", "BUILTIN\\Users", "Authenticated Users"):
                    self.assertNotIn(identity, permissions)
            else:
                self.assertEqual(stat.S_IMODE(folder.stat().st_mode), 0o700)
                self.assertEqual(stat.S_IMODE((folder / "ca.key").stat().st_mode), 0o600)
                self.assertEqual(stat.S_IMODE((folder / "settings.json").stat().st_mode), 0o600)
            acl = (folder / "broker.acl").read_text()
            self.assertNotIn("#", acl)
            self.assertNotIn("+", acl)
            sections = acl.split("user ")
            for identity, device in first["devices"].items():
                section = next(item for item in sections if item.startswith(device["username"] + "\n"))
                self.assertIn("topic readwrite " + identity, section)
                for other in first["devices"]:
                    if other != identity:
                        self.assertNotIn(other, section)
            context = ssl.create_default_context(cafile=str(folder / "ca.pem"))
            self.assertTrue(context.check_hostname)
            header = (folder / "include/lkbx_dev_config.h").read_text()
            self.assertIn('"https://127.0.0.1:8443"', header)
            self.assertNotIn("researchanddesire.com", header)
            self.assertIn("#ifndef VERSIONDEV", header)
            self.assertNotIn(first["controller_api_token"], header)
            self.assertNotIn(first["controller_mqtt"]["password"], header)
            changed = local.configure(root, "192.168.1.17")
            self.assertEqual(changed["devices"], first["devices"])
            self.assertNotEqual(first_cert, (folder / "server.pem").read_bytes())

    def test_cli_never_prints_secrets(self):
        with tempfile.TemporaryDirectory() as directory:
            capture = io.StringIO()
            with contextlib.redirect_stdout(capture):
                self.assertEqual(local.main(["configure", "--project-dir", directory]), 0)
            settings = json.loads((Path(directory) / ".dev/local/settings.json").read_text())
            for secret in (settings["controller_api_token"], settings["devices"][local.DEFAULT_DEVICE]["api_token"], settings["devices"][local.DEFAULT_DEVICE]["password"]):
                self.assertNotIn(secret, capture.getvalue())

    def test_custom_ports_survive_the_build_hook_reconfiguration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = local.configure(root, "192.168.1.17", api_port=18443, mqtt_port=18883)
            second = local.configure(root)
            self.assertEqual(first, second)
            header = (root / ".dev/local/include/lkbx_dev_config.h").read_text()
            self.assertIn('"https://192.168.1.17:18443"', header)
            self.assertIn("#define LKBX_DEV_MQTT_PORT 18883", header)
            environment = (root / ".dev/local/compose.env").read_text()
            self.assertIn("LKBX_DEV_API_PORT=18443", environment)
            self.assertIn("LKBX_DEV_MQTT_PORT=18883", environment)
        for port in (0, 65536, -1, True, "8883"):
            with self.subTest(port=port), self.assertRaises(ValueError):
                local.local_port(port)

    def test_selected_primary_survives_reconfiguration_with_retained_devices(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            local.configure(root)
            selected = "AA:BB:CC:DD:EE:FE"
            local.configure(root, device_ids=[selected])
            settings = local.configure(root)
            self.assertEqual(settings["primary_device"], selected)
            self.assertIn(local.DEFAULT_DEVICE, settings["devices"])
            header = (root / ".dev/local/include/lkbx_dev_config.h").read_text()
            self.assertIn('#define LKBX_DEV_DEVICE_ID "' + selected + '"', header)

    def test_certificate_supports_both_modern_ip_and_pinned_legacy_hostname_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            local.configure(root, "192.168.1.17")
            folder = root / ".dev/local"
            # OpenSSL's explicit hostname mode exercises the dNSName lookup
            # used by the pinned mbedTLS; IP mode checks the proper IP SAN.
            for name, kind in (("192.168.1.17", "hostname"), ("192.168.1.17", "ip"), ("127.0.0.1", "hostname"), ("127.0.0.1", "ip")):
                local.openssl("verify", "-CAfile", str(folder / "ca.pem"), "-verify_" + kind, name, str(folder / "server.pem"))
            ca_before = (folder / "ca.pem").read_bytes()
            key_before = (folder / "server.key").read_bytes()
            dates_before = ssl._ssl._test_decode_cert(str(folder / "server.pem"))
            settings = json.loads((folder / "settings.json").read_text())
            settings.pop("certificate_profile")
            (folder / "settings.json").write_text(json.dumps(settings))
            migrated = local.configure(root)
            self.assertEqual(migrated["certificate_profile"], local.CERTIFICATE_PROFILE)
            self.assertEqual((folder / "ca.pem").read_bytes(), ca_before)
            self.assertEqual((folder / "server.key").read_bytes(), key_before)
            dates_after = ssl._ssl._test_decode_cert(str(folder / "server.pem"))
            self.assertEqual(dates_after["notBefore"], dates_before["notBefore"])
            self.assertEqual(dates_after["notAfter"], dates_before["notAfter"])
            self.assertEqual(migrated["devices"], settings["devices"])

    def test_service_start_requires_development_device_target(self):
        for profile in ("development-r2", "development-r8"):
            self.assertFalse(local.should_start_services(profile, set()))
            self.assertFalse(local.should_start_services(profile, {"buildprog"}))
            self.assertTrue(local.should_start_services(profile, {"upload"}))
            self.assertTrue(local.should_start_services(profile, {"__test"}))
            self.assertFalse(local.should_start_services(profile, {"upload"}, ci=True))
        for profile in ("test", "test_native", "production-r2", "production-r8", "staging-r2", "staging-r8"):
            self.assertFalse(local.should_start_services(profile, {"upload", "test"}))

    def test_docker_failure_has_no_cloud_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / ".dev/local").mkdir(parents=True)
            (root / ".dev/local/compose.env").write_text("LKBX_DEV_BIND_IP=127.0.0.1\n")
            with patch.object(local.shutil, "which", return_value="docker"), patch.object(local.subprocess, "run") as run:
                run.return_value.returncode = 1
                with patch.object(local, "docker_environment", return_value={}), self.assertRaisesRegex(RuntimeError, "Cloud fallback is disabled"):
                    local.compose(root, "up")
                self.assertEqual(run.call_count, 1)
                self.assertIn(str(root.resolve() / "compose.yaml"), run.call_args.args[0])

    def test_public_image_client_never_inherits_credentials_or_remote_daemon(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with patch.dict(os.environ, {"DOCKER_HOST": "unix:///tmp/local-docker.sock", "DOCKER_CONTEXT": "desktop-local"}):
                environment = local.docker_environment(root, "docker")
            self.assertNotIn("DOCKER_CONTEXT", environment)
            self.assertEqual(environment["DOCKER_HOST"], "unix:///tmp/local-docker.sock")
            configuration = json.loads((Path(environment["DOCKER_CONFIG"]) / "config.json").read_text())
            self.assertEqual(configuration["auths"], {})
            self.assertNotIn("credsStore", configuration)
            with patch.dict(os.environ, {"DOCKER_HOST": "tcp://remote.example.com:2376"}), self.assertRaisesRegex(RuntimeError, "remote Docker"):
                local.docker_environment(root, "docker")

    def test_supported_profiles_are_explicit_and_build_only(self):
        parser = configparser.ConfigParser(interpolation=None)
        parser.read(ROOT / "platformio.ini")
        self.assertEqual({section for section in parser.sections() if section.startswith("env:")},
                         {"env:development-r2", "env:development-r8", "env:production-r2", "env:production-r8", "env:staging-r2", "env:staging-r8", "env:test", "env:test_native"})
        for section in parser.sections():
            self.assertFalse(parser.has_option(section, "targets"))
        self.assertIn(".dev/", (ROOT / ".gitignore").read_text())
        compose = (ROOT / "compose.yaml").read_text()
        self.assertNotIn("ca.key", compose)
        self.assertNotIn("./.dev/local:/", compose)
        self.assertIn('com.docker.network.bridge.enable_ip_masquerade: "false"', compose)


if __name__ == "__main__":
    unittest.main()
