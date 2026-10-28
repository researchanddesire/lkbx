"""PlatformIO hook: only development profiles receive local fixture configuration."""

Import("env")

import hashlib
import importlib.util
import os
from pathlib import Path

from SCons.Script import COMMAND_LINE_TARGETS

profile = env.subst("$PIOENV")
if profile in {"development-r2", "development-r8", "production-r2", "production-r8", "staging-r2", "staging-r8"}:
    root = Path(env.subst("$PROJECT_DIR"))
    spec = importlib.util.spec_from_file_location("lkbx_sdk_security", root / "scripts/patch_sdk_security.py")
    sdk_security = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk_security)
    sdk_security.install(env)

if profile in {"development-r2", "development-r8"}:
    root = Path(env.subst("$PROJECT_DIR"))
    spec = importlib.util.spec_from_file_location("lkbx_dev_services", root / "scripts/dev_services.py")
    local = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(local)
    # Like the other firmware repos' localhost replacement, follow this
    # machine's current LAN address unless LKBX_DEV_LAN_IP pins one.
    lan_ip = os.environ.get("LKBX_DEV_LAN_IP")
    if not lan_ip and not os.environ.get("CI"):
        lan_ip = local.detect_lan_ip()
    settings = local.configure(root, lan_ip=lan_ip)
    # A forced include keeps credentials out of compiler command lines/logs.
    header = root / ".dev/local/include/lkbx_dev_config.h"
    env.Append(CCFLAGS=["-include", str(header)])
    # SCons does not scan forced includes; a digest of the generated header
    # makes an address or credential change rebuild every object.
    digest = hashlib.sha256(header.read_bytes()).hexdigest()[:16]
    env.Append(CPPDEFINES=[("LKBX_DEV_CONFIG_DIGEST", "0x" + digest)])
    targets = set(COMMAND_LINE_TARGETS)
    if local.should_start_services(profile, targets, bool(os.environ.get("CI"))):
        if settings["lan_ip"] == "127.0.0.1":
            raise RuntimeError("Configure a reachable LAN address before device upload/test: python scripts/dev_services.py configure --lan-ip <private-IP>")
        local.compose(root, "up")
