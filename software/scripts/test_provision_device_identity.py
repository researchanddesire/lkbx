import contextlib
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))

import provision_device_identity as tool  # noqa: E402

BLANK = "00 " * 31 + "00"


def fuses(**overrides):
    summary = {}
    for index in range(6):
        summary[f"KEY_PURPOSE_{index}"] = {"value": "USER", "writeable": True, "readable": True}
        summary[f"BLOCK_KEY{index}"] = {"value": BLANK, "writeable": True, "readable": True}
    for name, value in overrides.items():
        summary[name] = {**summary[name], **value}
    return summary


class FakeEspefuse:
    """Stands in for every espefuse call, so these tests can never reach a device."""

    def __init__(self, before, after=None, burn_error=None):
        self.summaries = [before, after if after is not None else fuses(
            KEY_PURPOSE_5={"value": "HMAC_UP"},
            BLOCK_KEY5={"value": "?? " * 32, "readable": False, "writeable": False})]
        self.burn_error = burn_error
        self.calls = []
        self.key = None
        self.key_mode = None
        self.key_path = None

    def __call__(self, arguments, capture=False):
        self.calls.append(arguments)
        if "summary" in arguments:
            document = self.summaries.pop(0)
            return SimpleNamespace(stdout="espefuse.py v4\nConnecting...\n" + json.dumps(document))
        self.key_path = Path(arguments[arguments.index("burn_key") + 2])
        self.key = self.key_path.read_bytes()
        self.key_mode = stat.S_IMODE(os.stat(self.key_path).st_mode)
        if self.burn_error:
            raise self.burn_error
        return SimpleNamespace(stdout="")

    def burns(self):
        return [call for call in self.calls if "burn_key" in call]


def run_main(arguments, runner):
    output, errors = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
        code = tool.main(arguments, runner=runner)
    return code, output.getvalue(), errors.getvalue()


class ProvisionTests(unittest.TestCase):
    def test_refuses_without_the_permanent_flag_and_touches_nothing(self):
        runner = FakeEspefuse(fuses())
        code, _, errors = run_main(["--port", "/dev/fake"], runner)
        self.assertEqual(code, 2)
        self.assertEqual(runner.calls, [])
        self.assertIn(tool.PERMANENT_FLAG, errors)

    def test_requires_a_named_port(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            tool.main([tool.PERMANENT_FLAG], runner=FakeEspefuse(fuses()))

    def test_burns_one_random_key_and_keeps_no_copy(self):
        runner = FakeEspefuse(fuses())
        code, output, errors = run_main(["--port", "/dev/fake", tool.PERMANENT_FLAG], runner)
        self.assertEqual(code, 0, errors)
        burns = runner.burns()
        self.assertEqual(len(burns), 1)
        burn = burns[0]
        self.assertEqual(burn[:5], ["espefuse.py", "--chip", "esp32s3", "--port", "/dev/fake"])
        self.assertEqual(burn[5:7], ["burn_key", "BLOCK_KEY5"])
        self.assertEqual(burn[-1], "HMAC_UP")
        # The operator must still confirm, and the key stays read-protected.
        self.assertNotIn("--do-not-confirm", burn)
        self.assertNotIn("--no-read-protect", burn)
        self.assertEqual(len(runner.key), tool.KEY_BYTES)
        self.assertEqual(runner.key_mode, 0o600)
        self.assertFalse(runner.key_path.exists())
        self.assertNotIn(runner.key.hex(), output + errors)

    def test_keys_are_different_each_time(self):
        first, second = FakeEspefuse(fuses()), FakeEspefuse(fuses())
        run_main(["--port", "/dev/fake", tool.PERMANENT_FLAG], first)
        run_main(["--port", "/dev/fake", tool.PERMANENT_FLAG], second)
        self.assertNotEqual(first.key, second.key)

    def assert_refused(self, summary, *extra):
        runner = FakeEspefuse(summary)
        code, _, errors = run_main(["--port", "/dev/fake", tool.PERMANENT_FLAG, *extra], runner)
        self.assertEqual(code, 1, errors)
        self.assertEqual(runner.burns(), [])
        return errors

    def test_refuses_a_unit_that_already_has_an_identity_key(self):
        errors = self.assert_refused(fuses(KEY_PURPOSE_0={"value": "HMAC_UP"}))
        self.assertIn("already holds", errors)

    def test_refuses_a_block_with_another_purpose(self):
        self.assert_refused(fuses(KEY_PURPOSE_5={"value": "XTS_AES_128_KEY"}))

    def test_refuses_a_block_that_is_not_blank(self):
        self.assert_refused(fuses(BLOCK_KEY5={"value": "01 " + "00 " * 31}))

    def test_refuses_a_protected_block(self):
        self.assert_refused(fuses(BLOCK_KEY5={"readable": False}))
        self.assert_refused(fuses(BLOCK_KEY5={"writeable": False}))
        self.assert_refused(fuses(KEY_PURPOSE_5={"writeable": False}))

    def test_other_block_can_be_selected(self):
        runner = FakeEspefuse(fuses(), fuses(
            KEY_PURPOSE_3={"value": "HMAC_UP"},
            BLOCK_KEY3={"value": "?? " * 32, "readable": False, "writeable": False}))
        code, _, errors = run_main(
            ["--port", "/dev/fake", "--block", "BLOCK_KEY3", tool.PERMANENT_FLAG], runner)
        self.assertEqual(code, 0, errors)
        self.assertEqual(runner.burns()[0][6], "BLOCK_KEY3")

    def test_reports_a_burn_that_does_not_verify(self):
        runner = FakeEspefuse(fuses(), fuses())
        code, _, errors = run_main(["--port", "/dev/fake", tool.PERMANENT_FLAG], runner)
        self.assertEqual(code, 1)
        self.assertIn("did not verify", errors)

    def test_a_failed_burn_still_destroys_the_key_file(self):
        runner = FakeEspefuse(fuses(), burn_error=subprocess.CalledProcessError(1, "espefuse"))
        code, output, errors = run_main(["--port", "/dev/fake", tool.PERMANENT_FLAG], runner)
        self.assertEqual(code, 1)
        self.assertFalse(runner.key_path.exists())
        self.assertNotIn(runner.key.hex(), output + errors)

    def test_rejects_output_without_a_summary(self):
        runner = lambda arguments, capture=False: SimpleNamespace(stdout="no fuses here")
        code, _, errors = run_main(["--port", "/dev/fake", tool.PERMANENT_FLAG], runner)
        self.assertEqual(code, 1)
        self.assertIn("JSON summary", errors)


if __name__ == "__main__":
    unittest.main()
