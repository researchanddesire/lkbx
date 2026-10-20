"""PlatformIO hook: only development profiles receive local fixture configuration."""

Import("env")

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
    settings = local.configure(root, lan_ip=os.environ.get("LKBX_DEV_LAN_IP"))
    # A forced include keeps credentials out of compiler command lines/logs.
    header = root / ".dev/local/include/lkbx_dev_config.h"
    env.Append(CCFLAGS=["-include", str(header)])
    targets = set(COMMAND_LINE_TARGETS)
    if local.should_start_services(profile, targets, bool(os.environ.get("CI"))):
        if settings["lan_ip"] == "127.0.0.1":
            raise RuntimeError("Configure a reachable LAN address before device upload/test: python scripts/dev_services.py configure --lan-ip <private-IP>")
        local.compose(root, "up")
