#!/usr/bin/env python3
"""Exercise production lease/prompt transaction and exact SML display transitions."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1] / "firmware"
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++17 compiler with ASan/UBSan is required")
    with tempfile.TemporaryDirectory(prefix="lkbx-pairing-") as directory:
        for profile, origin in (("production", "https://dashboard.researchanddesire.com"),
                                ("staging", "https://staging.researchanddesire.com"),
                                ("development", "https://192.168.1.17:18443")):
            executable = Path(directory) / profile
            command = [compiler, "-std=c++17", "-g", "-O1", "-Wall", "-Wextra", "-Werror", "-pthread",
                   "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                   '-DRAD_SERVER="' + origin + '"',
                   "-I" + str(root / "test/test_pairing_refresh/mocks"),
                   "-I" + str(root / "src"), "-I" + str(root / "include"),
                   "-I" + str(root / "lib/LockboxSecurity/src"),
                   "-I" + str(root / ".pio/libdeps/test/ArduinoJson/src"),
                   str(root / "src/services/pairingCode.cpp"),
                   str(root / "test/test_pairing_refresh/main.cpp"), "-o", str(executable)]
            expected = origin
            if "clang" in Path(compiler).name.lower():
                # Vendored Boost.SML contains a GCC-only diagnostic pragma.
                command += ["-Wno-unknown-warning-option", "-fsanitize-ignorelist=" + str(root / "test/test_pairing_refresh/ubsan-ignorelist.txt")]
            if profile == "development":
                expected = "http://192.168.1.17:3000"
                command += ["-DVERSIONDEV", '-DLKBX_DEV_DASHBOARD_ORIGIN="' + expected + '"']
            command += ['-DEXPECTED_PAIRING_URL="' + expected + '?lkbx=ABCDEF"']
            subprocess.run(command, check=True)
            environment = os.environ.copy()
            environment["ASAN_OPTIONS"] = "halt_on_error=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
            subprocess.run([str(executable)], env=environment, check=True, timeout=15)
            print("Pairing refresh/environment profile passed:", profile)


if __name__ == "__main__":
    main()
