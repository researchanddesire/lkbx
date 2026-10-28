#!/usr/bin/env python3
"""Pure backport tests; --regression also checks the installed SDK parser with sanitizers."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import patch_sdk_security as security


class PatchTests(unittest.TestCase):
    def test_unreviewed_source_is_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "differs from reviewed"):
            security.patch_source(b"unreviewed SDK source")

    def test_prepare_source_is_local_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            framework = root / "global-framework"
            source = framework / security.SOURCE_RELATIVE
            source.parent.mkdir(parents=True)
            source.write_bytes(b"original")
            with patch.object(security, "patch_source", return_value=b"patched"):
                original, destination = security.prepare_source(root / "project", framework)
                timestamp = destination.stat().st_mtime_ns
                self.assertEqual(source.resolve(), original)
                self.assertEqual(destination.read_bytes(), b"patched")
                self.assertTrue(destination.is_relative_to((root / "project/.dev").resolve()))
                security.prepare_source(root / "project", framework)
                self.assertEqual(destination.stat().st_mtime_ns, timestamp)
            self.assertEqual(source.read_bytes(), b"original")


HARNESS_PREFIX = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef struct { uint32_t addr; } ip4_addr_t;
struct dhcps_state { u8_t state; };
static ip4_addr_t client_address;
static bool renew = false;
#define DHCPS_DEBUG 0
#define DHCP_OPTION_MSG_TYPE 53
#define DHCP_OPTION_REQ_IPADDR 50
#define DHCP_OPTION_END 255
#define DHCPDISCOVER 1
#define DHCPREQUEST 3
#define DHCPDECLINE 4
#define DHCPRELEASE 7
#define DHCPS_STATE_OFFER 1
#define DHCPS_STATE_ACK 3
#define DHCPS_STATE_NAK 4
#define DHCPS_STATE_IDLE 5
#define DHCPS_STATE_RELEASE 6
"""

HARNESS_SUFFIX = r"""
static void check(const u8_t *bytes, size_t size, u8_t expected) {
    u8_t *allocation = malloc(size ? size : 1);
    assert(allocation != NULL);
    if (size) memcpy(allocation, bytes, size);
    assert(parse_options(allocation, (s16_t)size) == expected);
    free(allocation);
}
#define CHECK(expected, ...) do { \
    const u8_t bytes[] = {__VA_ARGS__}; \
    check(bytes, sizeof(bytes), expected); \
} while (0)
int main(void) {
    const u8_t address[] = {1, 2, 3, 4};
    memcpy(&client_address.addr, address, 4);
    check(NULL, 0, DHCPS_STATE_IDLE);
    u8_t unused = 0;
    assert(parse_options(&unused, -1) == DHCPS_STATE_IDLE);
    CHECK(DHCPS_STATE_IDLE, 53);                 /* missing length */
    CHECK(DHCPS_STATE_IDLE, 53, 1);              /* missing type */
    CHECK(DHCPS_STATE_IDLE, 53, 0);              /* zero-sized type */
    CHECK(DHCPS_STATE_IDLE, 50, 4, 1, 2, 3);    /* truncated address */
    CHECK(DHCPS_STATE_IDLE, 50, 3, 1, 2, 3);    /* undersized address */
    CHECK(DHCPS_STATE_IDLE, 1, 255);             /* excessive length */
    CHECK(DHCPS_STATE_IDLE, 53, 1, 1, 50);      /* no partial acceptance */
    CHECK(DHCPS_STATE_IDLE, 0);                 /* single PAD */
    CHECK(DHCPS_STATE_IDLE, 255);               /* single END */
    CHECK(DHCPS_STATE_OFFER, 53, 1, 1, 255);
    CHECK(DHCPS_STATE_OFFER, 0, 0, 53, 1, 1, 0, 255);
    CHECK(DHCPS_STATE_OFFER, 200, 0, 201, 2, 1, 2, 53, 1, 1, 255);
    CHECK(DHCPS_STATE_OFFER, 53, 1, 1, 255, 53); /* bytes after END ignored */
    CHECK(DHCPS_STATE_ACK, 53, 1, 3, 50, 4, 1, 2, 3, 4, 255);
    CHECK(DHCPS_STATE_NAK, 53, 1, 3, 50, 4, 4, 3, 2, 1, 255);
    CHECK(DHCPS_STATE_RELEASE, 53, 1, 7, 255);
    CHECK(DHCPS_STATE_IDLE, 53, 1, 4, 255);
    renew = true;
    CHECK(DHCPS_STATE_ACK, 53, 1, 3, 255);
    return 0;
}
"""


def selected_framework(explicit: Path | None) -> Path:
    if explicit:
        return explicit
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", str(Path.home() / ".platformio")))
    for candidate in sorted((core / "packages").glob("framework-espidf*")):
        source = candidate / security.SOURCE_RELATIVE
        if source.is_file() and hashlib.sha256(source.read_bytes()).hexdigest() == security.SOURCE_SHA256:
            return candidate
    raise RuntimeError("Install the pinned firmware SDK by compiling a firmware profile first, or pass --framework-dir")


def regression(framework: Path) -> None:
    source = framework / security.SOURCE_RELATIVE
    original = source.read_bytes()
    before = hashlib.sha256(original).hexdigest()
    transformed = security.patch_source(original).decode()
    start = transformed.index("static u8_t parse_options(")
    end = transformed.index("/******************************************************************************", start)
    parser_source = transformed[start:end]
    compiler = os.environ.get("CC") or shutil.which("clang") or shutil.which("gcc")
    if not compiler:
        raise RuntimeError("A C compiler with address/undefined sanitizers is required")
    with tempfile.TemporaryDirectory(prefix="lkbx-dhcp-regression-") as temporary:
        folder = Path(temporary)
        harness = folder / "parser.c"
        harness.write_text(HARNESS_PREFIX + parser_source + HARNESS_SUFFIX)
        executable = folder / "parser-test"
        subprocess.run([compiler, "-std=c11", "-g", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(harness), "-o", str(executable)], check=True)
        environment = os.environ.copy()
        environment["ASAN_OPTIONS"] = "halt_on_error=1"
        environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
        subprocess.run([str(executable)], env=environment, check=True, timeout=15)
    if before != security.SOURCE_SHA256 or hashlib.sha256(source.read_bytes()).hexdigest() != before:
        raise RuntimeError("Global SDK source changed during regression verification")
    print("DHCP parser: 20 malformed/valid boundary cases passed under ASan/UBSan; pinned global SDK unchanged")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--regression", action="store_true")
    parser.add_argument("--framework-dir", type=Path)
    args, rest = parser.parse_known_args()
    if args.regression:
        regression(selected_framework(args.framework_dir))
    else:
        unittest.main(argv=[__file__, *rest])
