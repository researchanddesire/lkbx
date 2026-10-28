import json
import re
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from release_workflow import branch_configuration, main, tag_action


class ReleaseWorkflowTests(unittest.TestCase):
    def test_branch_configuration_maps_tracks_and_projects(self):
        self.assertEqual(
            branch_configuration("main")["PIO_ENV_R2"], "production-r2"
        )
        self.assertEqual(
            branch_configuration("main")["PIO_ENV_R8"], "production-r8"
        )
        self.assertEqual(
            branch_configuration("staging")["PIO_ENV_R2"], "staging-r2"
        )
        self.assertEqual(
            branch_configuration("staging")["PIO_ENV_R8"], "staging-r8"
        )
        self.assertNotEqual(
            branch_configuration("main")["STORAGE_PROJECT_REF"],
            branch_configuration("staging")["STORAGE_PROJECT_REF"],
        )
        with self.assertRaises(ValueError):
            branch_configuration("feature")

    def test_tag_action_is_idempotent_and_fails_on_collision(self):
        sha = "a" * 40
        self.assertEqual(tag_action("", sha), "create")
        self.assertEqual(tag_action(sha, sha), "exists")
        with self.assertRaisesRegex(ValueError, "different commit"):
            tag_action("b" * 40, sha)

    def test_configure_writes_github_environment(self):
        with tempfile.TemporaryDirectory() as directory:
            env_file = Path(directory) / "env"
            with patch.dict("os.environ", {"GITHUB_ENV": str(env_file)}), patch(
                "sys.argv", ["release_workflow.py", "configure", "--branch", "staging"]
            ):
                self.assertEqual(main(), 0)
            environment = env_file.read_text()
            self.assertIn("TRACK=staging", environment)
            self.assertIn("PIO_ENV_R2=staging-r2", environment)
            self.assertIn("PIO_ENV_R8=staging-r8", environment)

    def test_platformio_release_environments_are_variant_specific(self):
        repository = Path(__file__).resolve().parents[2]
        completed = subprocess.run(
            ["pio", "project", "config", "--json-output"],
            cwd=repository,
            text=True,
            stdout=subprocess.PIPE,
            check=True,
        )
        configuration = {
            section: dict(values) for section, values in json.loads(completed.stdout)
        }
        expected = {
            "env:staging-r2": (
                "esp32-s3-devkitc-1-n16r2v",
                "qio_qspi",
                'sdkconfig.defaults;sdkconfig.psram-r2.defaults',
            ),
            "env:staging-r8": (
                "esp32-s3-devkitc-1-n16r8v",
                "qio_opi",
                "sdkconfig.defaults",
            ),
            "env:production-r2": (
                "esp32-s3-devkitc-1-n16r2v",
                "qio_qspi",
                'sdkconfig.defaults;sdkconfig.psram-r2.defaults',
            ),
            "env:production-r8": (
                "esp32-s3-devkitc-1-n16r8v",
                "qio_opi",
                "sdkconfig.defaults",
            ),
        }
        for environment, (board, memory_type, sdkconfig_defaults) in expected.items():
            values = configuration[environment]
            self.assertEqual(values["board"], board)
            self.assertEqual(values["board_build.arduino.memory_type"], memory_type)
            self.assertIn(
                sdkconfig_defaults, values["board_build.cmake_extra_args"]
            )
        self.assertNotIn("env:staging", configuration)

    def test_release_control_changes_do_not_auto_publish_firmware(self):
        workflow = (
            Path(__file__).resolve().parents[1] / "workflows/publish_firmware.yml"
        ).read_text()
        push_paths = workflow.split("    paths:\n", 1)[1].split(
            "\n\npermissions:", 1
        )[0]
        configured_paths = [
            line.strip().removeprefix("- ").strip('"')
            for line in push_paths.splitlines()
            if line.strip().startswith("- ")
        ]
        self.assertEqual(
            configured_paths,
            [
                "CMakeLists.txt",
                "boards/**",
                "include/**",
                "lib/**",
                "src/**",
                "test/**",
                "platformio.ini",
                "partition.csv",
                "sdkconfig*",
                "pre_build_script.py",
                "firmware_build_metadata.py",
                "serial_identity_build.py",
                "scripts/patch_sdk_security.py",
            ],
        )

    def test_all_release_workflows_require_both_variants(self):
        workflows = Path(__file__).resolve().parents[1] / "workflows"
        publish = (workflows / "publish_firmware.yml").read_text()
        pull_request = (workflows / "pr-firmware.yml").read_text()
        finalize = (workflows / "finalize_release.yml").read_text()
        build_job = re.split(r"\n  \w", pull_request.split("  firmware:\n", 1)[1], maxsplit=1)[0]
        for track in ("staging", "production"):
            for variant in ("r2", "r8"):
                self.assertIn(f"environment: {track}-{variant}", build_job)
        self.assertIn('pio run -e "$PIO_ENV"', build_job)
        for variant in ("r2", "r8"):
            self.assertIn(f"--hardware-variant {variant}", publish)
            self.assertIn(f"{variant}_release_id:", finalize)
        self.assertIn("$PIO_ENV_R2", publish)
        self.assertIn("$PIO_ENV_R8", publish)
        self.assertIn("$release.hardwareVariant == $variant", finalize)

    def test_releases_publish_only_through_the_firmware_control_plane(self):
        workflows = Path(__file__).resolve().parents[1] / "workflows"
        for workflow in sorted(workflows.glob("*.yml")):
            text = workflow.read_text()
            with self.subTest(workflow=workflow.name):
                # Firmware is never published through GitHub Pages or docs/.
                self.assertNotIn("github.io", text)
                self.assertNotIn("pages:", text)
                self.assertNotRegex(text, r"(cp|mv|git add)[^\n]*\bdocs/")
        publish = (workflows / "publish_firmware.yml").read_text()
        self.assertIn("publish_immutable_firmware.py", publish)
        self.assertNotIn("git push origin \"HEAD:", publish.split("Allocate and commit next version", 1)[0])


if __name__ == "__main__":
    unittest.main()
