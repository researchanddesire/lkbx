#!/usr/bin/env python3
"""Apply the reviewed DHCP parser backport to an ignored copy of pinned ESP-IDF."""

from __future__ import annotations

import hashlib
import os
from pathlib import Path
import tempfile

SOURCE_RELATIVE = Path("components/lwip/apps/dhcpserver/dhcpserver.c")
SOURCE_SHA256 = "a7ff751cf191de080f60bf592de26a3b8768785c774e4ce9bd1b824b4cb7c8c4"
UPSTREAM_FIX = "https://github.com/espressif/esp-idf/commit/2da2db43fd7e0bcff9e7b95f54f388296bb6f911"

# Backport the upstream PAD/END and option bounds checks to ESP-IDF 4.4.7.
# Reject malformed options as an entire packet instead of retaining a message
# type parsed before a malformed option. Pointer subtraction avoids forming an
# out-of-bounds pointer while checking an attacker-controlled length.
OPTIONS_LOOP = """    while (optptr < end) {
        const u8_t option = *optptr;
        if (option == 0) { /* DHCP_OPTION_PAD */
            ++optptr;
            continue;
        }
        if (option == DHCP_OPTION_END) {
            break;
        }
        if (end - optptr < 2) {
            return DHCPS_STATE_IDLE;
        }
        const u8_t opt_len = optptr[1];
        if (opt_len > end - optptr - 2) {
            return DHCPS_STATE_IDLE;
        }
        switch (option) {
            case DHCP_OPTION_MSG_TYPE:
                if (opt_len < 1) {
                    return DHCPS_STATE_IDLE;
                }
                type = optptr[2];
                break;
            case DHCP_OPTION_REQ_IPADDR:
                if (opt_len < 4) {
                    return DHCPS_STATE_IDLE;
                }
                s.state = memcmp(&client.addr, optptr + 2, 4) == 0
                              ? DHCPS_STATE_ACK : DHCPS_STATE_NAK;
                break;
            default:
                break;
        }
        optptr += opt_len + 2;
    }

"""


def patch_source(original: bytes) -> bytes:
    if hashlib.sha256(original).hexdigest() != SOURCE_SHA256:
        raise RuntimeError("ESP-IDF DHCP source differs from reviewed 4.4.7; review the security backport before building")
    text = original.decode("utf-8")
    function = text.index("static u8_t parse_options(")
    start = text.index("    while (optptr < end) {", function)
    end = text.index("    switch (type) {", start)
    text = text[:start] + OPTIONS_LOOP + text[end:]
    text = text.replace("    bool is_dhcp_parse_end = false;\n", "", 1)
    text = text.replace("    u8_t *end = optptr + len;", "    if (len < 0) {\n        return DHCPS_STATE_IDLE;\n    }\n    u8_t *end = optptr + len;", 1)
    return text.encode("utf-8")


def prepare_source(project: Path, framework: Path) -> tuple[Path, Path]:
    original = (framework / SOURCE_RELATIVE).resolve()
    patched = patch_source(original.read_bytes())
    destination = project / ".dev/sdk-security/esp-idf-4.4.7/dhcpserver.c"
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_bytes() != patched:
        descriptor, temporary = tempfile.mkstemp(prefix=".dhcp-", dir=destination.parent)
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(patched)
            os.replace(temporary, destination)
        finally:
            if os.path.exists(temporary):
                os.unlink(temporary)
    return original, destination.resolve()


def install(env) -> None:
    project = Path(env.subst("$PROJECT_DIR"))
    framework = Path(env.PioPlatform().get_package_dir("framework-espidf"))
    original, patched = prepare_source(project, framework)
    # ESP-IDF components bypass PlatformIO's AddBuildMiddleware. Its SCons C
    # object emitter also covers those components and preserves compile groups.
    builder = env["BUILDERS"]["StaticObject"]
    previous = builder.emitter[".c"]

    def replace_dhcp_source(target, source, env):
        target, source = previous(target, source, env)
        if len(source) == 1 and Path(source[0].srcnode().get_abspath()).resolve() == original:
            source = [env.File(str(patched))]
            print("DHCP security backport: compiling project-local checked source")
        return target, source

    builder.add_emitter(".c", replace_dhcp_source)
