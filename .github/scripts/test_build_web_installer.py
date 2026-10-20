#!/usr/bin/env python3

import importlib.util
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

SCRIPT = Path(__file__).with_name("build_web_installer.py")
SPEC = importlib.util.spec_from_file_location("build_web_installer", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class BuildWebInstallerTests(unittest.TestCase):
    def test_lockbox_profile_is_esp32s3_with_16mb_flash(self):
        profile = MODULE.PROFILES["esp32s3-16mb"]
        self.assertEqual(profile.chip, "esp32s3")
        self.assertEqual(profile.chip_id, 9)
        self.assertEqual(profile.flash_size, "16MB")
        self.assertEqual(profile.flash_mode_id, 2)
        self.assertEqual(profile.flash_frequency_id, 0xF)
        self.assertEqual(profile.capacity, 16 * 1024 * 1024)
        self.assertEqual(profile.bootloader_offset, 0)

    def test_active_platformio_boot_app0_wins_over_stale_packages(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            active = (
                root
                / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
            )
            stale = (
                root
                / "packages/framework-arduinoespressif32@old/tools/partitions/boot_app0.bin"
            )
            active.parent.mkdir(parents=True)
            stale.parent.mkdir(parents=True)
            active.write_bytes(b"active")
            stale.write_bytes(b"stale")
            with mock.patch.dict(os.environ, {"PLATFORMIO_HOME_DIR": str(root)}):
                self.assertEqual(MODULE.find_boot_app0(), active)

    def test_merge_uses_platformio_esptool_and_profile(self):
        profile = MODULE.PROFILES["esp32s3-16mb"]
        esptool = Path("/platformio/packages/tool-esptoolpy/esptool.py")
        components = [(0, Path("bootloader.bin")), (0x10000, Path("firmware.bin"))]
        command = MODULE.merge_command(
            profile, esptool, Path("web-installer.bin"), components
        )
        self.assertEqual(command[:2], [sys.executable, str(esptool)])
        self.assertIn("esp32s3", command)
        self.assertIn("16MB", command)
        self.assertEqual(
            command[-4:], ["0x0", "bootloader.bin", "0x10000", "firmware.bin"]
        )

    def test_psram_variant_matches_generated_sdkconfig(self):
        with tempfile.TemporaryDirectory() as directory:
            build_dir = Path(directory)
            config_dir = build_dir / "config"
            config_dir.mkdir()
            sdkconfig = config_dir / "sdkconfig.h"
            sdkconfig.write_text("#define CONFIG_SPIRAM_MODE_QUAD 1\n")

            MODULE.validate_psram_variant(build_dir, "r2")
            with self.assertRaisesRegex(RuntimeError, "CONFIG_SPIRAM_MODE_OCT"):
                MODULE.validate_psram_variant(build_dir, "r8")

            sdkconfig.write_text(
                "#define CONFIG_SPIRAM_MODE_QUAD 1\n"
                "#define CONFIG_SPIRAM_MODE_OCT 1\n"
            )
            with self.assertRaisesRegex(RuntimeError, "unexpectedly enables"):
                MODULE.validate_psram_variant(build_dir, "r2")

    def test_build_components_rejects_wrong_chip(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            bootloader = bytearray(16)
            bootloader[0] = 0xE9
            bootloader[3] = 0x40
            bootloader[12:14] = (0).to_bytes(2, "little")
            (root / "bootloader.bin").write_bytes(bootloader)
            application = bytearray(16)
            application[0] = 0xE9
            application[12:14] = (9).to_bytes(2, "little")
            (root / "firmware.bin").write_bytes(application)
            (root / "partitions.bin").write_bytes(b"partitions")
            boot_app0 = root / "boot_app0.bin"
            boot_app0.write_bytes(b"boot-app")

            with self.assertRaisesRegex(RuntimeError, "chip ID"):
                MODULE.build_components(
                    MODULE.PROFILES["esp32s3-16mb"], root, boot_app0
                )

    def test_build_components_rejects_flash_header_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ("bootloader.bin", "firmware.bin"):
                image = bytearray(16)
                image[0] = 0xE9
                image[12:14] = (9).to_bytes(2, "little")
                (root / name).write_bytes(image)
            (root / "partitions.bin").write_bytes(b"partitions")
            boot_app0 = root / "boot_app0.bin"
            boot_app0.write_bytes(b"boot-app")

            with self.assertRaisesRegex(RuntimeError, "flash header"):
                MODULE.build_components(
                    MODULE.PROFILES["esp32s3-16mb"], root, boot_app0
                )

    def test_validation_checks_exact_components(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile = MODULE.PROFILES["esp32s3-16mb"]
            sources = {}
            for offset, name, content in (
                (0x0, "bootloader.bin", b"\xe9boot"),
                (0x8000, "partitions.bin", b"partition-table"),
                (0xE000, "boot_app0.bin", b"boot-app"),
                (0x10000, "firmware.bin", b"\xe9firmware"),
            ):
                path = root / name
                path.write_bytes(content)
                sources[offset] = path
            merged = bytearray(b"\xff" * 0x10020)
            for offset, path in sources.items():
                content = path.read_bytes()
                merged[offset : offset + len(content)] = content
            output = root / "web-installer.bin"
            output.write_bytes(merged)

            MODULE.validate_merged_image(output, profile, sorted(sources.items()))

            merged[0x8000] = 0
            output.write_bytes(merged)
            with self.assertRaisesRegex(RuntimeError, "component mismatch"):
                MODULE.validate_merged_image(output, profile, sorted(sources.items()))


if __name__ == "__main__":
    unittest.main()
