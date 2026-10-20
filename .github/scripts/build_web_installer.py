#!/usr/bin/env python3
"""Build and validate a merged ESP Web Tools factory image."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Profile:
    chip: str
    chip_id: int
    flash_size: str
    flash_size_id: int
    flash_mode_id: int
    flash_frequency_id: int
    capacity: int
    bootloader_offset: int


PROFILES = {
    "esp32s3-16mb": Profile(
        chip="esp32s3",
        chip_id=9,
        flash_size="16MB",
        flash_size_id=4,
        flash_mode_id=2,
        flash_frequency_id=0xF,
        capacity=16 * 1024 * 1024,
        bootloader_offset=0x0,
    ),
}

PSRAM_MODE_BY_VARIANT = {
    "r2": "CONFIG_SPIRAM_MODE_QUAD",
    "r8": "CONFIG_SPIRAM_MODE_OCT",
}


def require_image(path: Path, label: str) -> Path:
    if not path.is_file() or path.stat().st_size == 0:
        raise RuntimeError(f"{label} image is missing or empty: {path}")
    return path


def find_platformio_package_file(package: str, relative_path: str, label: str) -> Path:
    platformio_home = Path(
        os.environ.get("PLATFORMIO_HOME_DIR", Path.home() / ".platformio")
    )
    active_package = platformio_home / "packages" / package / relative_path
    if active_package.is_file():
        return require_image(active_package, label)

    candidates = sorted(
        platformio_home.glob(f"packages/{package}@*/{relative_path}")
    )
    if len(candidates) != 1:
        raise RuntimeError(
            f"expected one PlatformIO {label} file, found {len(candidates)}"
        )
    return require_image(candidates[0], label)


def find_boot_app0() -> Path:
    return find_platformio_package_file(
        "framework-arduinoespressif32",
        "tools/partitions/boot_app0.bin",
        "boot_app0",
    )


def find_esptool() -> Path:
    return find_platformio_package_file("tool-esptoolpy", "esptool.py", "esptool")


def validate_esp_image(path: Path, label: str, profile: Profile) -> None:
    content = require_image(path, label).read_bytes()
    if len(content) < 14 or content[0] != 0xE9:
        raise RuntimeError(f"{label} does not contain an ESP image header: {path}")
    chip_id = int.from_bytes(content[12:14], "little")
    if chip_id != profile.chip_id:
        raise RuntimeError(
            f"{label} targets chip ID {chip_id}, expected {profile.chip_id}"
        )


def validate_psram_variant(build_dir: Path, hardware_variant: str) -> None:
    sdkconfig = require_image(
        build_dir / "config" / "sdkconfig.h", "generated sdkconfig header"
    ).read_text(encoding="utf-8")
    expected = PSRAM_MODE_BY_VARIANT[hardware_variant]
    alternate = next(
        mode for variant, mode in PSRAM_MODE_BY_VARIANT.items()
        if variant != hardware_variant
    )
    if f"#define {expected} 1" not in sdkconfig:
        raise RuntimeError(
            f"{hardware_variant} web installer requires {expected}=1"
        )
    if f"#define {alternate} 1" in sdkconfig:
        raise RuntimeError(
            f"{hardware_variant} web installer unexpectedly enables {alternate}"
        )


def build_components(
    profile: Profile,
    build_dir: Path,
    boot_app0: Path,
) -> list[tuple[int, Path]]:
    bootloader = require_image(build_dir / "bootloader.bin", "bootloader")
    application = require_image(build_dir / "firmware.bin", "application")
    validate_esp_image(bootloader, "bootloader", profile)
    validate_esp_image(application, "application", profile)

    bootloader_header = bootloader.read_bytes()[:4]
    flash_mode_id = bootloader_header[2]
    flash_size_id = bootloader_header[3] >> 4
    flash_frequency_id = bootloader_header[3] & 0xF
    if (
        flash_mode_id != profile.flash_mode_id
        or flash_size_id != profile.flash_size_id
        or flash_frequency_id != profile.flash_frequency_id
    ):
        raise RuntimeError(
            "bootloader flash header does not match the selected profile: "
            f"found mode/size/frequency IDs {flash_mode_id}/{flash_size_id}/"
            f"{flash_frequency_id}, expected {profile.flash_mode_id}/"
            f"{profile.flash_size_id}/{profile.flash_frequency_id}"
        )

    components = [
        (profile.bootloader_offset, bootloader),
        (0x8000, require_image(build_dir / "partitions.bin", "partition table")),
        (0xE000, require_image(boot_app0, "boot_app0")),
        (0x10000, application),
    ]
    ordered = sorted(components)
    for (offset, path), (next_offset, _) in zip(ordered, ordered[1:]):
        if offset + path.stat().st_size > next_offset:
            raise RuntimeError(f"{path.name} overlaps the next flash component")
    final_offset, final_path = ordered[-1]
    if final_offset + final_path.stat().st_size > profile.capacity:
        raise RuntimeError("application extends beyond the physical flash capacity")
    return ordered


def merge_command(
    profile: Profile,
    esptool: Path,
    output: Path,
    components: list[tuple[int, Path]],
) -> list[str]:
    command = [
        sys.executable,
        str(esptool),
        "--chip",
        profile.chip,
        "merge_bin",
        "--output",
        str(output),
        "--flash_mode",
        "keep",
        "--flash_freq",
        "keep",
        "--flash_size",
        profile.flash_size,
    ]
    for offset, path in components:
        command.extend((hex(offset), str(path)))
    return command


def validate_merged_image(
    output: Path, profile: Profile, components: list[tuple[int, Path]]
) -> None:
    content = require_image(output, "merged web installer").read_bytes()
    if len(content) > profile.capacity:
        raise RuntimeError(
            f"merged image is {len(content)} bytes, beyond {profile.capacity}-byte flash"
        )

    for offset, source_path in components:
        source = source_path.read_bytes()
        if content[offset : offset + len(source)] != source:
            raise RuntimeError(f"merged component mismatch at {hex(offset)}")


def build(args: argparse.Namespace) -> None:
    profile = PROFILES[args.profile]
    boot_app0 = args.boot_app0 or find_boot_app0()
    validate_psram_variant(args.build_dir, args.hardware_variant)
    components = build_components(profile, args.build_dir, boot_app0)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        merge_command(profile, find_esptool(), args.output, components), check=True
    )
    validate_merged_image(args.output, profile, components)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--profile", required=True, choices=tuple(PROFILES))
    parser.add_argument(
        "--hardware-variant", required=True, choices=tuple(PSRAM_MODE_BY_VARIANT)
    )
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--boot-app0", type=Path)
    args = parser.parse_args()
    try:
        build(args)
        print(f"Built validated web installer: {args.output}")
        return 0
    except Exception as error:
        print(f"Web installer build failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
