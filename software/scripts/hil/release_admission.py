"""Isolated PR entry point. A local admission receipt binds this exact GitHub attempt."""
import argparse
import json
import time


def require_admission(receipt, expected, timeout=90, sleep=time.sleep):
    deadline = time.monotonic() + timeout
    while True:
        try:
            current = json.loads(receipt.read_text(encoding="utf-8"))
            remaining = current["expiresAt"] - time.time()
            if 0 < remaining <= 55 and all(current.get(k) == v for k, v in expected.items()):
                return
        except (OSError, ValueError, KeyError, TypeError):
            pass
        if time.monotonic() >= deadline:
            raise SystemExit("This GitHub run attempt has no active firmware queue reservation")
        sleep(1)


def check_from_argv(receipt):
    arguments = argparse.ArgumentParser(add_help=False)
    for key in ("repository", "build-sha"):
        arguments.add_argument("--" + key, required=True)
    for key in ("run-id", "run-attempt"):
        arguments.add_argument("--" + key, type=int, required=True)
    args, _ = arguments.parse_known_args()
    require_admission(receipt,
                      dict(repository=args.repository, buildSha=args.build_sha,
                           runId=args.run_id, runAttempt=args.run_attempt))
