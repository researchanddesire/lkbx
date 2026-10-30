#!/usr/bin/env python3
"""Burn a random HMAC identity key into one ESP32-S3 eFuse block. Manual, per device.

This is permanent: an eFuse cannot be erased and the unit can never be given a
different identity key. Firmware only ever reads the result, so nothing burns a
key except a person running this tool against one device on purpose. No copy of
the key is kept or printed, because the device derives its secret from it.
"""
import argparse
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile

BLOCKS = [f"BLOCK_KEY{index}" for index in range(6)]
KEY_BYTES = 32
PERMANENT_FLAG = "--i-understand-this-is-permanent"


class ProvisioningRefused(Exception):
    pass


def run_espefuse(arguments, capture=False):
    return subprocess.run(arguments, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None)


def read_summary(espefuse, port, runner):
    result = runner([espefuse, "--chip", "esp32s3", "--port", port, "summary", "--format", "json"],
                    capture=True)
    # espefuse prints its connection banner to stdout ahead of the JSON document.
    start = result.stdout.find("{")
    if start < 0:
        raise ProvisioningRefused("espefuse did not return a JSON summary")
    return json.loads(result.stdout[start:])


def purpose_name(block):
    return "KEY_PURPOSE_" + block.removeprefix("BLOCK_KEY")


def purpose_value(fuses, index):
    return str(fuses["KEY_PURPOSE_%d" % index]["value"]).split(" ", 1)[0]


def preflight(fuses, block):
    """Refuse anything but one blank, writable block on a unit with no identity key."""
    for index in range(6):
        if purpose_value(fuses, index) == "HMAC_UP":
            raise ProvisioningRefused(f"BLOCK_KEY{index} already holds an HMAC_UP identity key")
    purpose = fuses[purpose_name(block)]
    if str(purpose["value"]).split(" ", 1)[0] != "USER" or not purpose["writeable"]:
        raise ProvisioningRefused(f"{block} already has a key purpose")
    key = fuses[block]
    if not key["readable"] or not key["writeable"]:
        raise ProvisioningRefused(f"{block} is already protected")
    if str(key["value"]).replace(" ", "").strip("0") != "":
        raise ProvisioningRefused(f"{block} is not blank")


def provision(port, block, espefuse="espefuse.py", runner=run_espefuse):
    preflight(read_summary(espefuse, port, runner), block)
    with tempfile.TemporaryDirectory(prefix="lkbx-identity-") as directory:
        key_path = Path(directory) / "identity.key"
        descriptor = os.open(key_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as key_file:
            key_file.write(secrets.token_bytes(KEY_BYTES))
        try:
            # No --do-not-confirm: espefuse makes the operator type BURN as well.
            runner([espefuse, "--chip", "esp32s3", "--port", port, "burn_key", block,
                    str(key_path), "HMAC_UP"])
        finally:
            with open(key_path, "r+b") as key_file:
                key_file.write(b"\0" * KEY_BYTES)
    fuses = read_summary(espefuse, port, runner)
    if str(fuses[purpose_name(block)]["value"]).split(" ", 1)[0] != "HMAC_UP" or fuses[block]["readable"]:
        raise ProvisioningRefused(f"{block} did not verify as a read-protected HMAC_UP key")


def main(argv=None, runner=run_espefuse):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True, help="Serial port of the one device to provision")
    parser.add_argument("--block", choices=BLOCKS, default="BLOCK_KEY5")
    parser.add_argument("--espefuse", default="espefuse.py")
    parser.add_argument(PERMANENT_FLAG, dest="permanent", action="store_true",
                        help="Required. Burning an eFuse can never be undone.")
    arguments = parser.parse_args(argv)
    if not arguments.permanent:
        print(f"Refusing to burn an eFuse without {PERMANENT_FLAG}. This cannot be undone.",
              file=sys.stderr)
        return 2
    try:
        provision(arguments.port, arguments.block, arguments.espefuse, runner)
    except (ProvisioningRefused, subprocess.CalledProcessError) as error:
        print(f"Not provisioned: {error}", file=sys.stderr)
        return 1
    print(f"Provisioned {arguments.block} as a read-protected HMAC_UP identity key.")
    print("If this device was already enrolled, its owner must approve the new identity.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
