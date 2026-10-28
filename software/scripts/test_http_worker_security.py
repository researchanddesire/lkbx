#!/usr/bin/env python3
"""Compile and exercise the real worker using offline task/HTTP mocks and ASan/UBSan."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[1] / "firmware"
    tests = root / "test/test_http_worker_security"
    compiler = os.environ.get("CXX") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        raise SystemExit("A C++17 compiler with address/undefined sanitizers is required")
    with tempfile.TemporaryDirectory(prefix="lockbox-http-tests-") as temporary:
        for profile in ("production", "development"):
            executable = str(Path(temporary) / profile)
            command = [compiler, "-std=c++17", "-pthread", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                       "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                       "-DFIRMWARE_USE_IDF_CRT_BUNDLE", "-I" + str(tests / "mocks"),
                       "-I" + str(root / "src")]
            if profile == "development":
                command += ["-include", str(tests / "mocks/development_config.h")]
            else:
                command += ['-DRAD_SERVER="https://dashboard.example.test"']
            command += [str(root / "src/services/http_worker.cpp"),
                        str(root / "src/services/serverOnline.cpp"), str(tests / "test_main.cpp"),
                        "-o", executable]
            subprocess.run(command, check=True)
            environment = os.environ.copy()
            environment["ASAN_OPTIONS"] = "halt_on_error=1:detect_stack_use_after_return=1"
            environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
            subprocess.run([executable], check=True, env=environment, timeout=45)


if __name__ == "__main__":
    main()
