#!/usr/bin/env python3

import importlib.util
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).with_name("production_autodetect.py")
SPEC = importlib.util.spec_from_file_location("production_autodetect", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def efuse_dump(capacity_code: int, power_selection: int) -> bytes:
    words = [0] * 8
    words[0] = power_selection << 8
    words[6] = (capacity_code & 0x03) << 3
    words[7] = ((capacity_code >> 2) & 0x01) << 19
    return struct.pack("<8I", *words)


class HardwareDetectionTests(unittest.TestCase):
    def test_decodes_every_supported_fleet_tuple(self):
        expected = {
            (1, 0): "r2",
            (2, 0): "r2",
            (1, 1): "r8",
        }
        for values, variant in expected.items():
            with self.subTest(capacity=values[0], power=values[1]):
                identity = MODULE.decode_efuse_dump(efuse_dump(*values))
                self.assertEqual(identity.variant, variant)
                self.assertEqual(identity.psram_capacity_code, values[0])
                self.assertEqual(identity.pin_power_selection, values[1])

    def test_rejects_every_other_capacity_and_power_tuple(self):
        accepted = {(1, 0), (2, 0), (1, 1)}
        for capacity_code in range(8):
            for power_selection in range(2):
                if (capacity_code, power_selection) in accepted:
                    continue
                with self.subTest(
                    capacity=capacity_code, power=power_selection
                ), self.assertRaisesRegex(
                    MODULE.ProductionAutodetectError, "refusing to build or flash"
                ):
                    MODULE.decode_efuse_dump(
                        efuse_dump(capacity_code, power_selection)
                    )

    def test_rejects_truncated_register_dump(self):
        with self.assertRaisesRegex(
            MODULE.ProductionAutodetectError, "32-byte eFuse dump"
        ):
            MODULE.decode_efuse_dump(b"\0" * 28)

    def test_probe_uses_bundled_esptool_and_hard_resets(self):
        calls = []

        def runner(command, **kwargs):
            calls.append((command, kwargs))
            Path(command[-1]).write_bytes(efuse_dump(1, 1))
            return subprocess.CompletedProcess(command, 0, stdout="Done")

        identity = MODULE.probe_hardware(
            "/platformio/python",
            Path("/platformio/tool-esptoolpy/esptool.py"),
            "COM17",
            runner=runner,
        )

        self.assertEqual(identity.variant, "r8")
        command, kwargs = calls[0]
        self.assertEqual(command[0:2], ["/platformio/python", "/platformio/tool-esptoolpy/esptool.py"])
        self.assertIn("esp32s3", command)
        self.assertIn("COM17", command)
        self.assertIn("hard_reset", command)
        self.assertEqual(command[-3:-1], [hex(MODULE.EFUSE_DUMP_ADDRESS), "32"])
        self.assertNotIn("shell", kwargs)

    def test_probe_rejects_tool_failure_before_decoding(self):
        def runner(command, **kwargs):
            return subprocess.CompletedProcess(command, 2, stdout="wrong chip")

        with self.assertRaisesRegex(
            MODULE.ProductionAutodetectError, "wrong chip"
        ):
            MODULE.probe_hardware(
                sys.executable, Path("esptool.py"), "COM18", runner=runner
            )

    def test_probe_rejects_missing_tool_output(self):
        def runner(command, **kwargs):
            return subprocess.CompletedProcess(command, 0, stdout="Done")

        with self.assertRaisesRegex(
            MODULE.ProductionAutodetectError, "readable eFuse dump"
        ):
            MODULE.probe_hardware(
                sys.executable, Path("esptool.py"), "COM18", runner=runner
            )


class PortSelectionTests(unittest.TestCase):
    PORTS = [
        {
            "port": "/dev/cu.debug-console",
            "hwid": "n/a",
        },
        {
            "port": "/dev/cu.usbmodem2101",
            "hwid": "USB VID:PID=303A:1001 SER=ONE",
        },
    ]

    def test_honors_exact_explicit_port_without_guessing(self):
        self.assertEqual(MODULE.select_upload_port("COM27", []), "COM27")

    def test_resolves_unique_explicit_port_pattern(self):
        self.assertEqual(
            MODULE.select_upload_port("/dev/cu.usbmodem*", self.PORTS),
            "/dev/cu.usbmodem2101",
        )

    def test_auto_selects_one_native_esp32s3_port(self):
        self.assertEqual(
            MODULE.select_upload_port(None, self.PORTS),
            "/dev/cu.usbmodem2101",
        )

    def test_rejects_missing_native_port(self):
        with self.assertRaisesRegex(
            MODULE.ProductionAutodetectError, "pass --upload-port"
        ):
            MODULE.select_upload_port(None, self.PORTS[:1])

    def test_rejects_ambiguous_native_ports(self):
        ports = self.PORTS + [
            {
                "port": "/dev/cu.usbmodem3101",
                "hwid": "USB VID:PID=303A:1001 SER=TWO",
            }
        ]
        with self.assertRaisesRegex(
            MODULE.ProductionAutodetectError, "multiple ESP32-S3"
        ):
            MODULE.select_upload_port(None, ports)

    def test_rejects_ambiguous_explicit_pattern(self):
        ports = self.PORTS + [
            {
                "port": "/dev/cu.usbmodem3101",
                "hwid": "USB VID:PID=303A:1001 SER=TWO",
            }
        ]
        with self.assertRaisesRegex(
            MODULE.ProductionAutodetectError, "matched multiple"
        ):
            MODULE.select_upload_port("/dev/cu.usbmodem*", ports)


class DispatchTests(unittest.TestCase):
    def test_dispatches_only_production_builds(self):
        self.assertEqual(MODULE.dispatch_action("production", []), "detect")
        self.assertEqual(
            MODULE.dispatch_action("production", ["upload"]), "detect"
        )
        self.assertEqual(
            MODULE.dispatch_action("production-r8", ["upload"]), "skip"
        )

    def test_does_not_probe_for_introspection_or_clean(self):
        self.assertEqual(
            MODULE.dispatch_action("production", ["idedata"]), "skip"
        )
        self.assertEqual(
            MODULE.dispatch_action(
                "production", ["__idedata", "idedata"]
            ),
            "skip",
        )
        self.assertEqual(
            MODULE.dispatch_action("production", ["envdump"]), "skip"
        )
        self.assertEqual(
            MODULE.dispatch_action("production", ["clean"]), "clean"
        )
        self.assertEqual(
            MODULE.dispatch_action("production", ["fullclean"]), "clean"
        )
        self.assertEqual(
            MODULE.dispatch_action("production", [], is_clean=True), "clean"
        )

    def test_clean_handles_platformio_scons_option_without_probing(self):
        class Environment:
            def __init__(self, project_dir):
                self.values = {
                    "$PIOENV": "production",
                    "$PROJECT_DIR": str(project_dir),
                    "$PYTHONEXE": "/platformio/python",
                }
                self.exit_codes = []

            def subst(self, value):
                return self.values[value]

            def Exit(self, code):
                self.exit_codes.append(code)

        with tempfile.TemporaryDirectory() as directory:
            environment = Environment(directory)
            completed = subprocess.CompletedProcess(["pio"], 0)
            with mock.patch.object(
                MODULE.subprocess, "run", return_value=completed
            ) as run, mock.patch.object(MODULE, "probe_hardware") as probe:
                MODULE._run_platformio_hook(
                    environment,
                    [],
                    is_clean=True,
                    fullclean=True,
                    verbose=False,
                )

        probe.assert_not_called()
        self.assertEqual(environment.exit_codes, [])
        command = run.call_args.args[0]
        self.assertEqual(
            [command[index + 1] for index, value in enumerate(command) if value == "-e"],
            ["production-r2", "production-r8"],
        )
        self.assertEqual(command[-2:], ["-t", "fullclean"])

    def test_child_command_forwards_exact_port_and_targets(self):
        command = MODULE.platformio_command(
            "/platformio/python",
            Path("/checkout/lkbx"),
            ("production-r2",),
            ("upload",),
            upload_port="COM27",
            verbose=True,
        )
        self.assertEqual(
            command,
            [
                "/platformio/python",
                "-m",
                "platformio",
                "run",
                "--project-dir",
                "/checkout/lkbx",
                "-e",
                "production-r2",
                "--upload-port",
                "COM27",
                "-t",
                "upload",
                "--verbose",
            ],
        )

    def test_hook_propagates_selected_child_failure(self):
        class Platform:
            def __init__(self, directory):
                self.directory = directory

            def get_package_dir(self, package):
                self.asserted_package = package
                return self.directory

        class Environment:
            def __init__(self, project_dir, tool_dir):
                self.values = {
                    "$PIOENV": "production",
                    "$PROJECT_DIR": str(project_dir),
                    "$PYTHONEXE": "/platformio/python",
                    "$UPLOAD_PORT": "COM27",
                }
                self.platform = Platform(tool_dir)
                self.exit_codes = []

            def subst(self, value):
                return self.values[value]

            def PioPlatform(self):
                return self.platform

            def Exit(self, code):
                self.exit_codes.append(code)

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            tool_dir = root / "tool-esptoolpy"
            tool_dir.mkdir()
            (tool_dir / "esptool.py").touch()
            environment = Environment(root, tool_dir)
            completed = subprocess.CompletedProcess(["pio"], 9)
            identity = MODULE.HardwareIdentity("r2", 1, 0)
            with mock.patch.object(
                MODULE, "available_serial_ports",
                return_value=[],
            ), mock.patch.object(
                MODULE, "probe_hardware", return_value=identity
            ), mock.patch.object(
                MODULE.subprocess, "run", return_value=completed
            ) as run:
                MODULE._run_platformio_hook(
                    environment, ["upload"], verbose=False
                )

        self.assertEqual(environment.exit_codes, [9])
        child_command = run.call_args.args[0]
        self.assertIn("production-r2", child_command)
        self.assertIn("COM27", child_command)


class StandaloneCliTests(unittest.TestCase):
    def test_explicit_build_variant_never_probes_or_uploads(self):
        calls = []
        def runner(command, **kwargs):
            calls.append(command)
            return subprocess.CompletedProcess(command, 0)
        with mock.patch.object(MODULE, "probe_hardware") as probe:
            result = MODULE.main(["--variant", "r2", "--python", "/pio/python"], runner=runner)
        self.assertEqual(result, 0)
        probe.assert_not_called()
        self.assertEqual(len(calls), 1)
        self.assertIn("production-r2", calls[0])
        self.assertNotIn("upload", calls[0])
        self.assertNotIn("monitor", calls[0])
        self.assertNotIn("--upload-port", calls[0])

    def test_default_probes_then_builds_without_upload(self):
        calls = []
        def runner(command, **kwargs):
            calls.append(command)
            if "list" in command:
                return subprocess.CompletedProcess(command, 0, stdout=json.dumps(PortSelectionTests.PORTS))
            return subprocess.CompletedProcess(command, 0)
        with mock.patch.object(MODULE, "bundled_esptool", return_value=Path("/pio/esptool.py")), mock.patch.object(
            MODULE, "probe_hardware", return_value=MODULE.HardwareIdentity("r8", 1, 1)
        ) as probe:
            result = MODULE.main(["--python", "/pio/python"], runner=runner)
        self.assertEqual(result, 0)
        probe.assert_called_once()
        self.assertIn("production-r8", calls[-1])
        self.assertNotIn("upload", calls[-1])
        self.assertNotIn("monitor", calls[-1])

    def test_upload_requires_matching_probed_variant(self):
        with mock.patch.object(MODULE, "bundled_esptool", return_value=Path("/pio/esptool.py")), mock.patch.object(
            MODULE, "probe_hardware", return_value=MODULE.HardwareIdentity("r8", 1, 1)
        ) as probe:
            runner = mock.Mock(return_value=subprocess.CompletedProcess([], 0))
            result = MODULE.main(["--variant", "r2", "--upload", "--upload-port", "COM27", "--python", "/pio/python"], runner=runner)
            self.assertEqual(result, 1)
            probe.assert_called_once()
            runner.assert_not_called()

    def test_explicit_upload_adds_only_upload_target_and_propagates_failure(self):
        with mock.patch.object(MODULE, "bundled_esptool", return_value=Path("/pio/esptool.py")), mock.patch.object(
            MODULE, "probe_hardware", return_value=MODULE.HardwareIdentity("r2", 1, 0)
        ):
            runner = mock.Mock(return_value=subprocess.CompletedProcess([], 9))
            result = MODULE.main(["--upload", "--upload-port", "COM27", "--python", "/pio/python"], runner=runner)
        self.assertEqual(result, 9)
        command = runner.call_args.args[0]
        self.assertEqual(command[-2:], ["-t", "upload"])
        self.assertIn("COM27", command)
        self.assertNotIn("monitor", command)


if __name__ == "__main__":
    unittest.main()
