#!/usr/bin/env python3
"""Select and run the safe production environment for attached LKBX hardware."""

from __future__ import annotations

import fnmatch
import argparse
import importlib.util
import json
import os
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Sequence


ESP32S3_USB_HARDWARE_ID = "303A:1001"
EFUSE_DUMP_ADDRESS = 0x6000703C
EFUSE_DUMP_SIZE = 32
EFUSE_REPEAT_DATA3_OFFSET = 0
EFUSE_BLOCK1_WORD4_OFFSET = 0x18
EFUSE_BLOCK1_WORD5_OFFSET = 0x1C

PRODUCTION_ENVIRONMENTS = {
    "r2": "production-r2",
    "r8": "production-r8",
}
INTROSPECTION_TARGETS = frozenset({"idedata", "__idedata", "envdump"})
CLEAN_TARGETS = frozenset({"clean", "cleanall", "fullclean"})


class ProductionAutodetectError(RuntimeError):
    """Raised when the production dispatcher cannot make a safe selection."""


@dataclass(frozen=True)
class HardwareIdentity:
    variant: str
    psram_capacity_code: int
    pin_power_selection: int


def classify_hardware(
    psram_capacity_code: int, pin_power_selection: int
) -> HardwareIdentity:
    """Map the fleet's validated ESP32-S3 eFuse tuples to a firmware lane."""
    if pin_power_selection == 0 and psram_capacity_code in (1, 2):
        variant = "r2"
    elif pin_power_selection == 1 and psram_capacity_code == 1:
        variant = "r8"
    else:
        raise ProductionAutodetectError(
            "unsupported ESP32-S3 R2/R8 eFuse tuple "
            f"(PSRAM capacity code {psram_capacity_code}, "
            f"PIN_POWER_SELECTION {pin_power_selection}); refusing to build or flash"
        )
    return HardwareIdentity(variant, psram_capacity_code, pin_power_selection)


def decode_efuse_dump(data: bytes) -> HardwareIdentity:
    """Decode the relevant ESP32-S3 eFuse registers from a raw memory dump."""
    if len(data) != EFUSE_DUMP_SIZE:
        raise ProductionAutodetectError(
            f"expected a {EFUSE_DUMP_SIZE}-byte eFuse dump, received {len(data)} bytes"
        )

    repeat_data3 = struct.unpack_from("<I", data, EFUSE_REPEAT_DATA3_OFFSET)[0]
    block1_word4 = struct.unpack_from("<I", data, EFUSE_BLOCK1_WORD4_OFFSET)[0]
    block1_word5 = struct.unpack_from("<I", data, EFUSE_BLOCK1_WORD5_OFFSET)[0]
    psram_capacity_code = (
        ((block1_word5 >> 19) & 0x01) << 2
    ) | ((block1_word4 >> 3) & 0x03)
    pin_power_selection = (repeat_data3 >> 8) & 0x01
    return classify_hardware(psram_capacity_code, pin_power_selection)


def select_upload_port(
    configured_port: str | None, serial_ports: Iterable[dict[str, Any]]
) -> str:
    """Honor an explicit port or require exactly one native ESP32-S3 USB port."""
    ports = [
        {
            "port": str(item.get("port", "")).strip(),
            "hwid": str(item.get("hwid", "")).upper(),
        }
        for item in serial_ports
        if str(item.get("port", "")).strip()
    ]
    configured = (configured_port or "").strip()
    if configured:
        if not any(character in configured for character in "*?[]"):
            return configured
        matches = sorted(
            {item["port"] for item in ports if fnmatch.fnmatch(item["port"], configured)}
        )
        if len(matches) == 1:
            return matches[0]
        if not matches:
            raise ProductionAutodetectError(
                f"upload port pattern {configured!r} did not match a serial port"
            )
        raise ProductionAutodetectError(
            f"upload port pattern {configured!r} matched multiple serial ports: "
            f"{', '.join(matches)}"
        )

    candidates = sorted(
        {
            item["port"]
            for item in ports
            if ESP32S3_USB_HARDWARE_ID in item["hwid"]
        }
    )
    if len(candidates) == 1:
        return candidates[0]
    if not candidates:
        raise ProductionAutodetectError(
            "no ESP32-S3 USB serial port (VID:PID 303A:1001) found; "
            "connect one board or pass --upload-port"
        )
    raise ProductionAutodetectError(
        "multiple ESP32-S3 USB serial ports found; pass --upload-port to select one: "
        + ", ".join(candidates)
    )


def probe_hardware(
    python_executable: str,
    esptool: Path,
    port: str,
    *,
    runner: Any = subprocess.run,
) -> HardwareIdentity:
    """Read the required registers in one esptool connection and reset the board."""
    with tempfile.TemporaryDirectory(prefix="lkbx-production-") as directory:
        dump_path = Path(directory) / "efuse.bin"
        command = [
            python_executable,
            str(esptool),
            "--chip",
            "esp32s3",
            "--port",
            port,
            "--after",
            "hard_reset",
            "dump_mem",
            hex(EFUSE_DUMP_ADDRESS),
            str(EFUSE_DUMP_SIZE),
            str(dump_path),
        ]
        try:
            completed = runner(
                command,
                check=False,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                timeout=30,
            )
        except (OSError, subprocess.TimeoutExpired) as error:
            raise ProductionAutodetectError(
                f"could not run the bundled esptool on {port}: {error}"
            ) from error
        if completed.returncode != 0:
            detail = (completed.stdout or "esptool failed without output").strip()
            raise ProductionAutodetectError(
                f"could not read ESP32-S3 eFuses on {port}:\n{detail}"
            )
        try:
            data = dump_path.read_bytes()
        except OSError as error:
            raise ProductionAutodetectError(
                f"esptool did not produce a readable eFuse dump: {error}"
            ) from error
    return decode_efuse_dump(data)


def dispatch_action(
    environment: str, targets: Sequence[str], *, is_clean: bool = False
) -> str:
    """Return detect, clean, or skip for a PlatformIO/SCons invocation."""
    if environment != "production":
        return "skip"
    if is_clean:
        return "clean"
    target_set = {str(target) for target in targets}
    if target_set and target_set <= INTROSPECTION_TARGETS:
        return "skip"
    if target_set and target_set <= CLEAN_TARGETS:
        return "clean"
    return "detect"


def platformio_command(
    python_executable: str,
    project_dir: Path,
    environments: Sequence[str],
    targets: Sequence[str],
    *,
    upload_port: str | None = None,
    verbose: bool = False,
) -> list[str]:
    """Build a shell-free child PlatformIO command."""
    command = [
        python_executable,
        "-m",
        "platformio",
        "run",
        "--project-dir",
        str(project_dir),
    ]
    for environment in environments:
        command.extend(("-e", environment))
    if upload_port:
        command.extend(("--upload-port", upload_port))
    for target in targets:
        command.extend(("-t", str(target)))
    if verbose:
        command.append("--verbose")
    return command


def platformio_python(explicit: str | None = None) -> str:
    """Use the requested interpreter, active PlatformIO environment, or standard penv."""
    if explicit:
        return explicit
    if importlib.util.find_spec("platformio") is not None:
        return sys.executable
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio")))
    executable = core / "penv" / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    if executable.is_file():
        return str(executable)
    raise ProductionAutodetectError("PlatformIO is unavailable; run this script from its Python environment or pass --python")


def bundled_esptool(explicit: Path | None = None) -> Path:
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio")))
    path = explicit or core / "packages/tool-esptoolpy/esptool.py"
    if not path.is_file():
        raise ProductionAutodetectError("PlatformIO's tool-esptoolpy is unavailable; install the production profile dependencies or pass --esptool")
    return path


def listed_ports(python_executable: str, runner: Any) -> list[dict[str, Any]]:
    result = runner([python_executable, "-m", "platformio", "device", "list", "--json-output"],
                    check=False, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=15)
    if result.returncode:
        raise ProductionAutodetectError("PlatformIO could not enumerate serial ports; use its Python environment or pass an exact --upload-port")
    try:
        ports = json.loads(result.stdout)
        if not isinstance(ports, list) or any(not isinstance(item, dict) for item in ports):
            raise ValueError("unexpected serial-port listing")
    except (ValueError, TypeError) as error:
        raise ProductionAutodetectError("PlatformIO returned an invalid serial-port listing") from error
    return ports


def available_serial_ports():
    from platformio.device.list.util import list_serial_ports
    return list_serial_ports()


def main(argv: Sequence[str] | None = None, *, runner: Any = None) -> int:
    parser = argparse.ArgumentParser(
        description="Select the R2/R8 production build from a connected ESP32-S3's eFuse identity. Probing reads registers and resets the board; flashing requires --upload.",
        epilog="For a build without connecting hardware, pass --variant r2 or --variant r8. Production builds use RAD Cloud. This helper never monitors or uploads by default.")
    parser.add_argument("--variant", choices=tuple(PRODUCTION_ENVIRONMENTS), help="Known hardware variant for build-only; verified against eFuses when --upload is requested")
    parser.add_argument("--upload", action="store_true", help="Explicitly flash the verified connected board after building")
    parser.add_argument("--upload-port", help="Exact serial port or unambiguous pattern; otherwise require one ESP32-S3 USB device")
    parser.add_argument("--project-dir", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--python", help="Python interpreter containing PlatformIO")
    parser.add_argument("--esptool", type=Path, help="Explicit bundled esptool.py path")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args(argv)
    runner = runner or subprocess.run
    try:
        python_executable = platformio_python(args.python)
        variant = args.variant
        port = None
        if variant is None or args.upload:
            exact_port = args.upload_port and not any(character in args.upload_port for character in "*?[]")
            ports = [] if exact_port else listed_ports(python_executable, runner)
            port = select_upload_port(args.upload_port, ports)
            identity = probe_hardware(python_executable, bundled_esptool(args.esptool), port, runner=runner)
            if variant is not None and variant != identity.variant:
                raise ProductionAutodetectError(f"Requested {variant.upper()} but connected hardware is {identity.variant.upper()}; refusing to flash")
            variant = identity.variant
            print(f"Detected LKBX {variant.upper()} on {port}")
        selected = PRODUCTION_ENVIRONMENTS[variant]
        print(f"Building {selected}" + (" and uploading to the verified device" if args.upload else ""))
        command = platformio_command(python_executable, args.project_dir.resolve(), (selected,),
                                     ("upload",) if args.upload else (), upload_port=port if args.upload else None,
                                     verbose=args.verbose)
        result = runner(command, cwd=args.project_dir.resolve(), check=False)
        return result.returncode
    except (ProductionAutodetectError, OSError, subprocess.TimeoutExpired) as error:
        print(f"Production selection failed: {error}", file=sys.stderr)
        return 1


def _run_platformio_hook(
    scons_env: Any,
    targets: Sequence[str],
    *,
    is_clean: bool = False,
    fullclean: bool = False,
    verbose: bool,
) -> None:
    action = dispatch_action(
        scons_env.subst("$PIOENV"), targets, is_clean=is_clean
    )
    if action == "skip":
        return

    project_dir = Path(scons_env.subst("$PROJECT_DIR"))
    python_executable = scons_env.subst("$PYTHONEXE") or sys.executable
    if action == "clean":
        clean_target = "fullclean" if fullclean else "clean"
        command = platformio_command(
            python_executable,
            project_dir,
            tuple(PRODUCTION_ENVIRONMENTS.values()),
            (clean_target,),
            verbose=verbose,
        )
    else:
        try:
            port = select_upload_port(
                scons_env.subst("$UPLOAD_PORT"), available_serial_ports()
            )
            tool_dir = scons_env.PioPlatform().get_package_dir("tool-esptoolpy")
            if not tool_dir:
                raise ProductionAutodetectError(
                    "PlatformIO's bundled tool-esptoolpy package is unavailable"
                )
            esptool = Path(tool_dir) / "esptool.py"
            if not esptool.is_file():
                raise ProductionAutodetectError(
                    f"PlatformIO's bundled esptool was not found at {esptool}"
                )
            identity = probe_hardware(python_executable, esptool, port)
        except ProductionAutodetectError as error:
            sys.stderr.write(f"Error: Production hardware auto-detection failed: {error}\n")
            scons_env.Exit(1)
            return

        selected_environment = PRODUCTION_ENVIRONMENTS[identity.variant]
        power_source = (
            "VDD_SPI" if identity.pin_power_selection else "VDD3P3_CPU"
        )
        print(
            f"Detected LKBX {identity.variant.upper()} on {port} "
            f"(PSRAM capacity code {identity.psram_capacity_code}, {power_source})"
        )
        print(f"Switching production profile to {selected_environment}")
        command = platformio_command(
            python_executable,
            project_dir,
            (selected_environment,),
            targets,
            upload_port=port,
            verbose=verbose,
        )

    try:
        completed = subprocess.run(command, cwd=project_dir, check=False)
    except OSError as error:
        sys.stderr.write(f"Error: Could not start selected PlatformIO build: {error}\n")
        scons_env.Exit(1)
        return
    if action == "clean" and completed.returncode == 0:
        # Let PlatformIO finish cleaning the dispatcher environment itself.
        return
    scons_env.Exit(completed.returncode)


# PlatformIO executes extra scripts through SCons, which supplies Import as a global.
# Ordinary imports (including unit tests) intentionally stop before this hook.
if "Import" in globals():
    globals()["Import"]("env")
    from SCons.Script import ARGUMENTS, COMMAND_LINE_TARGETS

    _run_platformio_hook(
        globals()["env"],
        [str(target) for target in COMMAND_LINE_TARGETS],
        is_clean=bool(globals()["env"].GetOption("clean")),
        fullclean=bool(int(ARGUMENTS.get("FULLCLEAN", 0))),
        verbose=bool(int(ARGUMENTS.get("PIOVERBOSE", 0))),
    )
elif __name__ == "__main__":
    raise SystemExit(main())
