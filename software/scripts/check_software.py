#!/usr/bin/env python3
"""Check release software boundaries and scan software without inspecting docs."""

import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOFTWARE_ROOTS = {"software"}
SOFTWARE_FILES = {".gitignore", ".github/workflows/software-ci.yml"}
# Before software/ held them, the same sources lived at the repository root.
HISTORY_PATHS = {"src", "include", "lib", "boards", "test", "scripts", "dev",
                 "platformio.ini", "CMakeLists.txt", "partition.csv",
                 "sdkconfig.defaults", "sdkconfig.psram-r2.defaults",
                 "pre_build_script.py", "firmware_build_metadata.py", "compose.yaml"}
DOC_SUFFIXES = {".md", ".mdx", ".rst"}


def is_software(path):
    p = Path(path)
    if p.suffix.lower() in DOC_SUFFIXES or p.name in {"README", "LICENSE"}:
        return False
    return p.parts[0] in SOFTWARE_ROOTS or path in SOFTWARE_FILES


def paths():
    return subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT).decode().split("\0")


def check(history=False):
    tracked = [p for p in paths() if p and is_software(p)]
    forbidden = []
    for name in tracked:
        p = ROOT / name
        if not p.exists():
            continue
        if p.is_symlink() or p.suffix.lower() in {".bin", ".elf", ".o", ".pem", ".key", ".p12"} or \
                any(part in {".dev", ".pio", "node_modules", ".git"} for part in Path(name).parts) or \
                Path(name).name.startswith(".env"):
            forbidden.append(name)
    if forbidden:
        raise SystemExit("Private/generated files in software index: " + ", ".join(forbidden))
    if not shutil.which("gitleaks"):
        raise SystemExit("gitleaks 8.30.1 is required for the software secret scan")
    with tempfile.TemporaryDirectory(prefix="lockbox-software-scan-") as tmp:
        export = Path(tmp)
        for name in tracked:
            p = ROOT / name
            if p.is_file():
                target = export / name
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(p, target)
        subprocess.run(["gitleaks", "dir", str(export), "--redact=100", "--no-banner"], check=True)
    if history:
        # Git pathspecs cover the retained software ancestry only. Documentation
        # and hardware are intentionally reserved for their later release pass.
        options = "--all -- " + " ".join(sorted(SOFTWARE_ROOTS | SOFTWARE_FILES | HISTORY_PATHS)) + \
                  " :(exclude)**/*.md :(exclude)**/*.mdx :(exclude)**/*.rst :(exclude)**/README"
        subprocess.run(["gitleaks", "git", str(ROOT), "--redact=100", "--no-banner",
                        "--log-opts=" + options], check=True)
    print("Software boundary and secret checks passed.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", action="store_true")
    check(parser.parse_args().history)
