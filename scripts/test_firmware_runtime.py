#!/usr/bin/env python3
"""Run the unchanged OTA stream verifier with synthetic transport and flash sinks."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    tests = root / "test/test_firmware_runtime"
    library = root / "lib/FirmwareUpdateProtocol/src"
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++17 compiler with sanitizers is required")
    crypto = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", "openssl"], text=True))
    with tempfile.TemporaryDirectory(prefix="lockbox-stream-tests-") as temporary:
        directory = Path(temporary)
        # Copy exact current bytes so its quoted provenance include resolves to
        # the isolated stub. There is no second test implementation of the gate.
        shutil.copyfile(library / "FirmwareUpdateRuntime.h", directory / "FirmwareUpdateRuntime.h")
        executable = directory / "stream-tests"
        command = [compiler, "-std=c++17", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   "-DARDUINO_ARCH_ESP32", "-DFIRMWARE_USE_IDF_CRT_BUNDLE",
                   "-I" + str(directory), "-I" + str(tests / "mocks"), "-I" + str(library),
                   "-I" + str(root / ".pio/libdeps/test/ArduinoJson/src"),
                   str(tests / "main.cpp"), "-o", str(executable), *crypto]
        subprocess.run(command, check=True)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(executable)], check=True, env=environment, timeout=15)


if __name__ == "__main__":
    main()
