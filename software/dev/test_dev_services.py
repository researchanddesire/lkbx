#!/usr/bin/env python3
"""Local fixture configuration tests; never require Docker or a physical device."""

import configparser
import contextlib
import ctypes
import ctypes.util
import importlib.util
import io
import json
import os
from pathlib import Path
import ssl
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch

import dev_services as local

ROOT = Path(__file__).resolve().parent
REPOSITORY = ROOT.parents[1]
FIRMWARE = ROOT.parent / "firmware"


def legacy_x509_input_is_self_signed(path):
    """Exercise the OpenSSL 3.0 micro-CA input gate with real libcrypto.

    This models apps/x509.c's self_signed/callb policy, not an older CLI.
    https://github.com/openssl/openssl/blob/openssl-3.0.13/apps/x509.c
    """
    library = (Path(sys.base_prefix) / "DLLs/libcrypto-3.dll" if os.name == "nt"
               else ctypes.util.find_library("crypto"))
    if not library or (isinstance(library, Path) and not library.exists()):
        raise unittest.SkipTest("OpenSSL shared library required for the real legacy input gate")
    crypto = ctypes.CDLL(str(library))
    ptr = ctypes.c_void_p
    callback_type = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ptr)

    def bind(name, arguments, result):
        function = getattr(crypto, name)
        function.argtypes, function.restype = arguments, result
        return function

    bio_open = bind("BIO_new_file", [ctypes.c_char_p, ctypes.c_char_p], ptr)
    read_cert = bind("PEM_read_bio_X509", [ptr, ptr, ptr, ptr], ptr)
    store_new = bind("X509_STORE_new", [], ptr)
    ctx_new = bind("X509_STORE_CTX_new", [], ptr)
    ctx_init = bind("X509_STORE_CTX_init", [ptr, ptr, ptr, ptr], ctypes.c_int)
    set_flags = bind("X509_STORE_CTX_set_flags", [ptr, ctypes.c_ulong], None)
    set_callback = bind("X509_STORE_set_verify_cb", [ptr, callback_type], None)
    get_error = bind("X509_STORE_CTX_get_error", [ptr], ctypes.c_int)
    verify = bind("X509_verify_cert", [ptr], ctypes.c_int)
    release = [("X509_STORE_CTX_free", None), ("X509_STORE_free", None),
               ("X509_free", None), ("BIO_free", ctypes.c_int)]
    cleanup = [bind(name, [ptr], result) for name, result in release]
    bio = bio_open(os.fsencode(path), b"r")
    cert = read_cert(bio, None, None, None) if bio else None
    store, ctx = store_new(), ctx_new()

    @callback_type
    def callback(ok, current):
        # Depth-zero self-signature is permitted only for this CLI input gate;
        # a successfully verified CA-signed leaf is deliberately rejected.
        return 1 if get_error(current) == 18 or not ok else 0

    try:
        if not all((bio, cert, store, ctx)):
            raise RuntimeError("Could not initialize legacy OpenSSL input-gate test")
        set_callback(store, callback)
        # OpenSSL copies the store callback when initializing the context.
        if ctx_init(ctx, store, cert, None) != 1:
            raise RuntimeError("Could not initialize legacy OpenSSL input-gate callback")
        set_flags(ctx, 0x4000)  # X509_V_FLAG_CHECK_SS_SIGNATURE
        return verify(ctx) > 0
    finally:
        for free, resource in zip(cleanup, (ctx, store, cert, bio)):
            if resource:
                free(resource)


class ConfigurationTests(unittest.TestCase):
    def test_addresses_cannot_select_cloud_or_dns(self):
        for value in ("8.8.8.8", "dashboard.researchanddesire.com", "0.0.0.0", "169.254.169.254", "100.64.0.1", "::1", "224.0.0.1", "192.168.1.255"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                local.local_address(value)
        self.assertEqual(local.local_address("192.168.1.17"), "192.168.1.17")

    def test_detected_lan_address_is_private_or_absent(self):
        for detected, expected in (("192.168.1.195", "192.168.1.195"), ("8.8.4.4", None), ("127.0.0.1", None)):
            with self.subTest(detected=detected), patch.object(local.socket, "socket") as factory:
                factory.return_value.__enter__.return_value.getsockname.return_value = (detected, 50000)
                self.assertEqual(local.detect_lan_ip(), expected)
        with patch.object(local.socket, "socket", side_effect=OSError):
            self.assertIsNone(local.detect_lan_ip())

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

    def test_pairing_dashboard_is_local_and_survives_build_hook(self):
        for value in ("https://dashboard.researchanddesire.com", "http://8.8.8.8:3000", "http://u:p@192.168.1.17:3000", "http://192.168.1.17/path", "http://192.168.1.17?secret=x", "file:///tmp/app", "http://127.0.0.1:0"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                local.local_dashboard_origin(value)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = local.configure(root, "192.168.1.17", api_port=18443, dashboard_origin="http://192.168.1.18:3100/")
            self.assertEqual(first["dashboard_origin"], "http://192.168.1.18:3100")
            self.assertEqual(first, local.configure(root))
            header = (root / ".dev/local/include/lkbx_dev_config.h").read_text()
            self.assertIn('#define LKBX_DEV_DASHBOARD_ORIGIN "http://192.168.1.18:3100"', header)

    def test_certificate_supports_both_modern_ip_and_pinned_legacy_hostname_checks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            local.configure(root, "192.168.1.17")
            folder = root / ".dev/local"
            # Recreate the actual old profile: proper IP SANs, no IP dNSNames.
            local.write_private(folder / "server.ext", "basicConstraints=critical,CA:FALSE\n"
                                "keyUsage=critical,digitalSignature,keyEncipherment\n"
                                "extendedKeyUsage=serverAuth\nsubjectAltName=IP:192.168.1.17,IP:127.0.0.1\n")
            local.openssl("x509", "-req", "-in", str(folder / "server.csr"), "-CA", str(folder / "ca.pem"),
                          "-CAkey", str(folder / "ca.key"), "-set_serial", "123", "-days", "365", "-sha256",
                          "-extfile", str(folder / "server.ext"), "-out", str(folder / "server.pem"))
            # OpenSSL's explicit hostname mode exercises the dNSName lookup
            # used by the pinned mbedTLS; IP mode checks the proper IP SAN.
            for name, kind in (("192.168.1.17", "hostname"), ("192.168.1.17", "ip"), ("127.0.0.1", "hostname"), ("127.0.0.1", "ip")):
                if kind == "hostname":
                    with self.assertRaises(RuntimeError):
                        local.openssl("verify", "-CAfile", str(folder / "ca.pem"), "-verify_hostname", name, str(folder / "server.pem"))
                else:
                    local.openssl("verify", "-CAfile", str(folder / "ca.pem"), "-verify_ip", name, str(folder / "server.pem"))
            ca_before = (folder / "ca.pem").read_bytes()
            ca_key_before = (folder / "ca.key").read_bytes()
            key_before = (folder / "server.key").read_bytes()
            dates_before = ssl._ssl._test_decode_cert(str(folder / "server.pem"))
            settings = json.loads((folder / "settings.json").read_text())
            settings.pop("certificate_profile")
            (folder / "settings.json").write_text(json.dumps(settings))
            migrated = local.configure(root)
            self.assertEqual(migrated["certificate_profile"], local.CERTIFICATE_PROFILE)
            self.assertEqual((folder / "ca.pem").read_bytes(), ca_before)
            self.assertEqual((folder / "ca.key").read_bytes(), ca_key_before)
            self.assertEqual((folder / "server.key").read_bytes(), key_before)
            dates_after = ssl._ssl._test_decode_cert(str(folder / "server.pem"))
            self.assertEqual(dates_after["notBefore"], dates_before["notBefore"])
            self.assertEqual(dates_after["notAfter"], dates_before["notAfter"])
            self.assertEqual(migrated["devices"], settings["devices"])
            for name, kind in (("192.168.1.17", "hostname"), ("192.168.1.17", "ip"),
                               ("127.0.0.1", "hostname"), ("127.0.0.1", "ip"),
                               ("localhost", "hostname"), ("mosquitto", "hostname")):
                local.openssl("verify", "-CAfile", str(folder / "ca.pem"), "-purpose", "sslserver",
                              "-verify_" + kind, name, str(folder / "server.pem"))
            migrated_leaf = (folder / "server.pem").read_bytes()
            self.assertEqual(local.configure(root), migrated)
            self.assertEqual((folder / "server.pem").read_bytes(), migrated_leaf)

    def test_migration_accepts_real_openssl_3_0_self_signed_input_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            local.configure(root, "192.168.1.17")
            folder = root / ".dev/local"
            self.assertFalse(legacy_x509_input_is_self_signed(folder / "server.pem"))
            real_openssl, checked = local.openssl, []

            def legacy_openssl(*args):
                if args[0] == "x509" and "-CA" in args and "-req" not in args:
                    candidate = Path(args[args.index("-in") + 1])
                    self.assertTrue(legacy_x509_input_is_self_signed(candidate))
                    checked.append(candidate)
                return real_openssl(*args)

            with patch.object(local, "openssl", side_effect=legacy_openssl):
                local.certificate_files(folder, "192.168.1.17", preserve_leaf=True)
            self.assertEqual(len(checked), 1)
            self.assertFalse(checked[0].exists())  # transient self-signed stage removed
            self.assertFalse(legacy_x509_input_is_self_signed(folder / "server.pem"))
            self.assertEqual(list(folder.glob("certificate-migration-*")), [])

    def test_migration_preserves_original_on_real_hostname_verification_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            local.configure(root, "192.168.1.17")
            folder = root / ".dev/local"
            local.write_private(folder / "server.ext", (folder / "server.ext").read_text() + "# previous profile\n")
            preserved = {name: (folder / name).read_bytes()
                         for name in ("ca.pem", "ca.key", "server.key", "server.pem", "server.ext", "settings.json")}
            real_openssl = local.openssl

            def wrong_hostname(*args):
                if args[0] == "verify" and "-verify_hostname" in args:
                    arguments = list(args)
                    arguments[args.index("-verify_hostname") + 1] = "192.168.1.18"
                    return real_openssl(*arguments)
                return real_openssl(*args)

            # Exercise a real failed CA/name check, rather than only raising
            # from a mock. The original server.pem remains usable throughout.
            with patch.object(local, "openssl", side_effect=wrong_hostname), self.assertRaises(RuntimeError):
                local.certificate_files(folder, "192.168.1.17", preserve_leaf=True)
            for name, contents in preserved.items():
                self.assertEqual((folder / name).read_bytes(), contents)
            self.assertEqual(list(folder.glob("certificate-migration-*")), [])

    def test_migration_refuses_existing_key_mismatch_without_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            local.configure(root, "192.168.1.17")
            folder = root / ".dev/local"
            # These are disposable test keys, never enrolled fixture secrets.
            local.write_private(folder / "server.key", (folder / "ca.key").read_text())
            local.write_private(folder / "server.ext", (folder / "server.ext").read_text() + "# previous profile\n")
            preserved = {name: (folder / name).read_bytes()
                         for name in ("ca.pem", "ca.key", "server.key", "server.pem", "server.ext", "settings.json")}
            with self.assertRaisesRegex(RuntimeError, "do not match"):
                local.certificate_files(folder, "192.168.1.17", preserve_leaf=True)
            for name, contents in preserved.items():
                self.assertEqual((folder / name).read_bytes(), contents)
            self.assertEqual(list(folder.glob("certificate-migration-*")), [])

    def test_migration_refuses_changed_validity_without_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            local.configure(root, "192.168.1.17")
            folder = root / ".dev/local"
            local.write_private(folder / "server.ext", (folder / "server.ext").read_text() + "# previous profile\n")
            preserved = {name: (folder / name).read_bytes()
                         for name in ("ca.pem", "ca.key", "server.key", "server.pem", "server.ext", "settings.json")}
            real_openssl = local.openssl

            def changed_dates(*args):
                if args[0] == "x509" and "-CA" in args:
                    arguments = [arg for arg in args if arg != "-preserve_dates"]
                    return real_openssl(*arguments, "-days", "1")
                return real_openssl(*args)

            with patch.object(local, "openssl", side_effect=changed_dates), self.assertRaisesRegex(RuntimeError, "validity interval"):
                local.certificate_files(folder, "192.168.1.17", preserve_leaf=True)
            for name, contents in preserved.items():
                self.assertEqual((folder / name).read_bytes(), contents)
            self.assertEqual(list(folder.glob("certificate-migration-*")), [])


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
                with patch.object(local, "docker_environment", return_value={}), \
                        patch.object(local, "rad_app_problem", return_value="EMQX is not running"), \
                        contextlib.redirect_stdout(io.StringIO()), \
                        self.assertRaisesRegex(RuntimeError, "Cloud fallback is disabled"):
                    local.compose(root, "up")
                self.assertEqual(run.call_count, 1)
                self.assertIn(str(root.resolve() / "compose.yaml"), run.call_args.args[0])

    def test_backend_falls_back_to_mosquitto_without_local_rad_app(self):
        with patch.object(local, "rad_app_problem", return_value="EMQX is not running"), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(local.select_backend("auto"), "fixture")
        self.assertIn("Mosquitto", output.getvalue())
        with patch.object(local, "rad_app_problem", return_value=None), contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(local.select_backend("auto"), "rad-app")
            self.assertEqual(local.select_backend("rad-app"), "rad-app")
        with patch.object(local, "rad_app_problem", return_value="EMQX is not running"), \
                self.assertRaisesRegex(RuntimeError, "EMQX is not running"):
            local.select_backend("rad-app")
        # CI and an explicit fixture request never probe the machine at all.
        with patch.object(local, "rad_app_problem", side_effect=AssertionError("probed")):
            self.assertEqual(local.select_backend("auto", ci=True), "fixture")
            self.assertEqual(local.select_backend("fixture"), "fixture")
        with self.assertRaises(ValueError):
            local.select_backend("cloud")

    def test_rad_app_detection_only_probes_loopback(self):
        probes = []

        def reply(status, body):
            def get(port, path, timeout):
                probes.append((port, path))
                return status, body
            return get

        with patch.object(local.http.client, "HTTPConnection") as connection:
            connection.return_value.getresponse.return_value.status = 200
            connection.return_value.getresponse.return_value.read.return_value = b"emqx is running"
            self.assertIsNone(local.rad_app_problem())
            self.assertEqual({call.args[0] for call in connection.call_args_list}, {"127.0.0.1"})
            self.assertEqual({call.args[1] for call in connection.call_args_list}, {local.EMQX_STATUS_PORT, local.RAD_APP_PORT})
        with patch.object(local, "loopback_get", side_effect=reply(200, b"Node emqx@127.0.0.1 is stopped")):
            self.assertEqual(local.rad_app_problem(), "EMQX is not running")
        with patch.object(local, "loopback_get", side_effect=ConnectionRefusedError):
            self.assertEqual(local.rad_app_problem(), "EMQX is not running")
        answers = iter([(200, b"emqx is running"), ConnectionRefusedError()])

        def emqx_without_app(port, path, timeout):
            answer = next(answers)
            if isinstance(answer, Exception):
                raise answer
            return answer

        with patch.object(local, "loopback_get", side_effect=emqx_without_app):
            self.assertIn("RAD App is not answering", local.rad_app_problem())
        self.assertEqual(probes, [(local.EMQX_STATUS_PORT, "/status")])

    def test_up_stops_both_backends_before_starting_one(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / ".dev/local").mkdir(parents=True)
            (root / ".dev/local/compose.env").write_text("LKBX_DEV_BIND_IP=127.0.0.1\n")
            for backend in ("fixture", "rad-app"):
                with self.subTest(backend=backend), patch.object(local.shutil, "which", return_value="docker"), \
                        patch.object(local.subprocess, "run") as run, patch.object(local, "docker_environment", return_value={}), \
                        patch.object(local, "select_backend", return_value=backend) as select:
                    run.return_value.returncode = 0
                    local.compose(root, "up")
                    down, up = (call.args[0] for call in run.call_args_list)
                    self.assertEqual(down[down.index("down") - 4:down.index("down")], ["--profile", "fixture", "--profile", "rad-app"])
                    self.assertEqual(up[up.index("up") - 2:up.index("up")], ["--profile", backend])
                    self.assertEqual(up.count("--profile"), 1)
            with patch.object(local.shutil, "which", return_value="docker"), patch.object(local.subprocess, "run") as run, \
                    patch.object(local, "docker_environment", return_value={}), \
                    patch.dict(os.environ, {"LKBX_DEV_BACKEND": "fixture", "CI": ""}), \
                    patch.object(local, "rad_app_problem", side_effect=AssertionError("probed")):
                run.return_value.returncode = 0
                local.compose(root, "up")
                self.assertIn(["--profile", "fixture", "up"], [call.args[0][-8:-5] for call in run.call_args_list])

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
        parser.read(FIRMWARE / "platformio.ini")
        self.assertEqual({section for section in parser.sections() if section.startswith("env:")},
                         {"env:development-r2", "env:development-r8", "env:production-r2", "env:production-r8", "env:staging-r2", "env:staging-r8", "env:test", "env:test_native"})
        for section in parser.sections():
            self.assertFalse(parser.has_option(section, "targets"))
        self.assertIn(".dev/", (REPOSITORY / ".gitignore").read_text())
        compose = (ROOT / "compose.yaml").read_text()
        self.assertNotIn("ca.key", compose)
        self.assertNotIn("./.dev/local:/", compose)
        self.assertIn('com.docker.network.bridge.enable_ip_masquerade: "false"', compose)
        # The RAD App front holds the TLS leaf only, never fixture credentials.
        front = compose[compose.index("  rad-app:"):compose.index("\nnetworks:")]
        for private in ("settings.json", "broker-passwords", "broker.acl"):
            self.assertNotIn(private, front)
        self.assertIn('"--upstream", "host.docker.internal"', front)


if __name__ == "__main__":
    unittest.main()
