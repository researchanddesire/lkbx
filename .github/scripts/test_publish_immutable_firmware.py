import argparse
import hashlib
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import publish_immutable_firmware as publisher

from publish_immutable_firmware import (
    parse_artifact,
    read_version,
    validate_artifacts,
    validate_bridge_reproducibility_report,
    validate_hardware_variant,
    validate_upload_namespace,
)


class PublisherTests(unittest.TestCase):
    def test_reads_numeric_semantic_version(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "Version.h"
            path.write_text('#define VERSION "1.2.3"\n')
            self.assertEqual(read_version(path), "1.2.3")

    def test_rejects_missing_artifact(self):
        with self.assertRaises(argparse.ArgumentTypeError):
            parse_artifact("application:missing.bin:1:true")

    def test_parses_installable_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "firmware.bin"
            path.write_bytes(b"firmware")
            artifact = parse_artifact(f"application:{path}:2:true")
            self.assertTrue(artifact.installable)
            self.assertEqual(artifact.install_order, 2)
            self.assertEqual(artifact.sha256, "c3bf47ea1f4a4a605470313cacb3a44f4a461f68c6faeab07e737610cb5ac835")

    def test_lkbx_requires_explicit_psram_variant(self):
        validate_hardware_variant("lkbx", "r2")
        validate_hardware_variant("lkbx", "r8")
        with self.assertRaisesRegex(RuntimeError, "r2, r8"):
            validate_hardware_variant("lkbx", "default")
        validate_hardware_variant(
            "lkbx", "default", kind="migration", version="1.21.19"
        )
        for kind, version in (
            ("firmware", "1.21.19"),
            ("migration", "1.21.18"),
            ("migration", "1.21.20"),
        ):
            with self.assertRaisesRegex(RuntimeError, "reserved"):
                validate_hardware_variant(
                    "lkbx", "default", kind=kind, version=version
                )
        with self.assertRaisesRegex(RuntimeError, "default"):
            validate_hardware_variant("dtt", "r8")

    def test_lkbx_upload_prefix_is_variant_scoped(self):
        self.assertEqual(
            validate_upload_namespace(
                "lkbx", "r2", "abcdef1", "releases/1.2.3/abcdef1/r2"
            ),
            "releases/1.2.3/abcdef1/r2",
        )
        with self.assertRaisesRegex(RuntimeError, "hardware variant"):
            validate_upload_namespace(
                "lkbx", "r2", "abcdef1", "releases/1.2.3/abcdef1/r8"
            )

    def test_lkbx_bridge_upload_prefix_uses_migration_namespace(self):
        self.assertEqual(
            validate_upload_namespace(
                "lkbx",
                "default",
                "abcdef1",
                "releases/1.21.19/abcdef1/migration",
                "migration",
            ),
            "releases/1.21.19/abcdef1/migration",
        )
        with self.assertRaisesRegex(RuntimeError, "release kind"):
            validate_upload_namespace(
                "lkbx",
                "default",
                "abcdef1",
                "releases/1.21.19/abcdef1",
                "migration",
            )
        with self.assertRaisesRegex(RuntimeError, "release kind"):
            validate_upload_namespace(
                "lkbx",
                "default",
                "abcdef1",
                "releases/1.21.19/abcdef1/default/migration",
                "migration",
            )

    def test_lkbx_bridge_requires_matching_two_build_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifacts = []
            report_artifacts = []
            for index, (role, filename, installable) in enumerate(
                (
                    ("application", "firmware.bin", True),
                    ("bootloader", "bootloader.bin", False),
                    ("partitions", "partitions.bin", False),
                    ("web-installer", "web-installer.bin", False),
                ),
                start=1,
            ):
                path = root / filename
                path.write_bytes(filename.encode())
                artifact = parse_artifact(
                    f"{role}:{path}:{index}:{str(installable).lower()}"
                )
                artifacts.append(artifact)
                report_artifacts.append(
                    {
                        "filename": filename,
                        "sha256": artifact.sha256,
                        "sizeBytes": artifact.size_bytes,
                    }
                )
            report = root / "reproducibility.json"
            report.write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "candidateCommit": "a" * 40,
                        "sourceDateEpoch": 1788022800,
                        "artifacts": report_artifacts,
                    }
                ),
                encoding="utf-8",
            )
            args = argparse.Namespace(
                device_type="lkbx",
                hardware_variant="default",
                kind="migration",
                build_sha="a" * 40,
                artifact=artifacts,
                reproducibility_report=report,
            )
            validate_bridge_reproducibility_report(args, "1.21.19")
            args.reproducibility_report = None
            with self.assertRaisesRegex(RuntimeError, "two-build"):
                validate_bridge_reproducibility_report(args, "1.21.19")
            args.reproducibility_report = report
            (root / "firmware.bin").write_bytes(b"different")
            with self.assertRaisesRegex(RuntimeError, "firmware.bin"):
                validate_bridge_reproducibility_report(args, "1.21.19")

    def test_lkbx_bridge_rejects_swapped_artifact_roles(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifacts = []
            report_artifacts = []
            for index, (role, filename, installable) in enumerate(
                (
                    ("bootloader", "firmware.bin", False),
                    ("application", "bootloader.bin", True),
                    ("partitions", "partitions.bin", False),
                    ("web-installer", "web-installer.bin", False),
                ),
                start=1,
            ):
                path = root / filename
                path.write_bytes(filename.encode())
                artifact = parse_artifact(
                    f"{role}:{path}:{index}:{str(installable).lower()}"
                )
                artifacts.append(artifact)
                report_artifacts.append(
                    {
                        "filename": filename,
                        "sha256": artifact.sha256,
                        "sizeBytes": artifact.size_bytes,
                    }
                )
            report = root / "reproducibility.json"
            report.write_text(
                json.dumps(
                    {
                        "schemaVersion": 1,
                        "candidateCommit": "a" * 40,
                        "sourceDateEpoch": 1788022800,
                        "artifacts": report_artifacts,
                    }
                ),
                encoding="utf-8",
            )
            args = argparse.Namespace(
                device_type="lkbx",
                hardware_variant="default",
                kind="migration",
                build_sha="a" * 40,
                artifact=artifacts,
                reproducibility_report=report,
            )
            with self.assertRaisesRegex(RuntimeError, "metadata"):
                validate_bridge_reproducibility_report(args, "1.21.19")

    def test_rejects_installable_web_installer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            application = root / "firmware.bin"
            application.write_bytes(b"application")
            web_installer = root / "web-installer.bin"
            web_installer.write_bytes(b"factory")
            artifacts = [
                parse_artifact(f"application:{application}:1:true"),
                parse_artifact(f"web-installer:{web_installer}:2:true"),
            ]
            with self.assertRaisesRegex(RuntimeError, "non-installable"):
                validate_artifacts(artifacts)

    def test_requires_one_web_installer(self):
        with tempfile.TemporaryDirectory() as directory:
            application = Path(directory) / "firmware.bin"
            application.write_bytes(b"application")
            with self.assertRaisesRegex(RuntimeError, "exactly one"):
                validate_artifacts(
                    [parse_artifact(f"application:{application}:1:true")]
                )

    def test_publish_stores_web_installer_but_excludes_it_from_ota(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            version_file = root / "Version.h"
            version_file.write_text('#define VERSION "1.2.3"\n', encoding="utf-8")
            artifact_specs = (
                ("application", "firmware.bin", 1, True),
                ("bootloader", "bootloader.bin", 2, False),
                ("partitions", "partitions.bin", 3, False),
                ("web-installer", "web-installer.bin", 4, False),
            )
            artifacts = []
            for role, filename, order, installable in artifact_specs:
                path = root / filename
                path.write_bytes(filename.encode())
                artifacts.append(
                    parse_artifact(
                        f"{role}:{path}:{order}:{str(installable).lower()}"
                    )
                )

            requests = []
            canonical_provenance_bytes = (
                b'{"provenance":"canonical.token.value"}\n'
            )
            canonical_provenance_sha256 = hashlib.sha256(
                canonical_provenance_bytes
            ).hexdigest()

            def create_provenance(_args, _version, _manifest, _application, path, order):
                path.write_text('{"provenance":"test.token.value"}\n')
                return publisher.Artifact("provenance", path, order, False), "test.token.value"

            def request_json(url, _token, payload):
                requests.append((url, payload))
                if url.endswith("/uploads"):
                    prefix = "releases/1.2.3/abcdef1/r8"
                    return {
                        "objectPrefix": prefix,
                        "provenance": "canonical.token.value",
                        "uploads": [
                            {
                                "filename": artifact["filename"],
                                "signedUrl": (
                                    None
                                    if artifact["filename"]
                                    in {"web-installer.bin", "provenance.json"}
                                    else f"https://upload.test/{artifact['filename']}"
                                ),
                                "publicUrl": f"https://storage.test/{artifact['filename']}",
                                "objectPath": f"{prefix}/{artifact['filename']}",
                                "sha256": (
                                    canonical_provenance_sha256
                                    if artifact["filename"] == "provenance.json"
                                    else artifact["sha256"]
                                ),
                                "sizeBytes": (
                                    len(canonical_provenance_bytes)
                                    if artifact["filename"] == "provenance.json"
                                    else artifact["sizeBytes"]
                                ),
                                "alreadyExists": (
                                    artifact["filename"]
                                    in {"web-installer.bin", "provenance.json"}
                                ),
                            }
                            for artifact in payload["artifacts"]
                        ],
                    }
                if url.endswith("/releases"):
                    return {"releaseId": "00000000-0000-0000-0000-000000000001"}
                return {}

            args = argparse.Namespace(
                device_type="lkbx",
                hardware_variant="r8",
                track="staging",
                build_sha="abcdef1",
                version_file=version_file,
                kind="firmware",
                artifact=artifacts,
            )
            environment = {
                "FIRMWARE_PUBLISH_TOKEN": "test-token",
                "RUNNER_TEMP": str(root / "runner"),
            }

            def verify_public_object(url, _sha256, _size_bytes):
                if url.endswith("/provenance.json"):
                    return canonical_provenance_bytes
                return b"verified"

            with (
                mock.patch.dict(os.environ, environment, clear=False),
                mock.patch.object(publisher, "request_json", side_effect=request_json),
                mock.patch.object(publisher, "upload_file") as upload_file,
                mock.patch.object(
                    publisher,
                    "verify_public_object",
                    side_effect=verify_public_object,
                ) as verify_public_object,
                mock.patch.object(
                    publisher, "create_provenance", side_effect=create_provenance
                ),
            ):
                publisher.publish(args)

            upload_request = next(
                payload for url, payload in requests if url.endswith("/uploads")
            )
            release_request = next(
                payload for url, payload in requests if url.endswith("/releases")
            )
            self.assertIn(
                "web-installer",
                {artifact["role"] for artifact in upload_request["artifacts"]},
            )
            self.assertEqual(upload_request["kind"], "firmware")
            self.assertEqual(upload_request["hardwareVariant"], "r8")
            self.assertEqual(upload_request["provenance"], "test.token.value")
            self.assertEqual(release_request["hardwareVariant"], "r8")
            self.assertEqual(
                [artifact["role"] for artifact in release_request["artifacts"]],
                ["application", "web-installer", "manifest", "provenance"],
            )
            self.assertEqual(
                release_request["provenance"], "canonical.token.value"
            )
            provenance_release_artifact = next(
                artifact
                for artifact in release_request["artifacts"]
                if artifact["role"] == "provenance"
            )
            self.assertEqual(
                provenance_release_artifact["sha256"],
                canonical_provenance_sha256,
            )
            self.assertEqual(
                provenance_release_artifact["sizeBytes"],
                len(canonical_provenance_bytes),
            )
            self.assertEqual(upload_file.call_count, 5)
            self.assertEqual(verify_public_object.call_count, 7)
            provenance_verify = next(
                call
                for call in verify_public_object.call_args_list
                if call.args[0].endswith("/provenance.json")
            )
            self.assertEqual(provenance_verify.args[1], canonical_provenance_sha256)
            self.assertEqual(
                provenance_verify.args[2], len(canonical_provenance_bytes)
            )
            self.assertNotIn(
                "https://upload.test/web-installer.bin",
                [call.args[0] for call in upload_file.call_args_list],
            )
            self.assertNotIn(
                "https://upload.test/provenance.json",
                [call.args[0] for call in upload_file.call_args_list],
            )
            self.assertEqual(
                (root / "runner/firmware-release/r8/provenance.json").read_bytes(),
                canonical_provenance_bytes,
            )
            manifest = json.loads(
                (root / "runner/firmware-release/r8/manifest.json").read_text()
            )
            self.assertEqual(manifest["hardwareVariant"], "r8")
            self.assertEqual(
                [artifact["role"] for artifact in manifest["artifacts"]],
                ["application", "bootloader", "partitions", "web-installer"],
            )

    def test_release_includes_manifest_and_provenance(self):
        application = publisher.Artifact(
            "application", Path("firmware.bin"), 2, True
        )
        manifest = publisher.Artifact("manifest", Path("manifest.json"), 5, False)
        provenance = publisher.Artifact(
            "provenance", Path("provenance.json"), 7, False
        )
        self.assertEqual(
            [
                artifact.role
                for artifact in publisher.complete_release_artifacts(
                    [application], manifest, provenance
                )
            ],
            ["application", "manifest", "provenance"],
        )

    def test_legacy_envelope_is_limited_to_old_production_schema(self):
        old_schema = publisher.ControlPlaneHttpError(
            400,
            '{"issues":[{"code":"invalid_value","values":'
            '["application","manifest","release"],"path":'
            '["artifacts",6,"role"]}]}',
        )
        self.assertTrue(
            publisher.requires_legacy_production_envelope("main", old_schema)
        )
        self.assertFalse(
            publisher.requires_legacy_production_envelope("staging", old_schema)
        )
        self.assertFalse(
            publisher.requires_legacy_production_envelope(
                "main",
                publisher.ControlPlaneHttpError(
                    400,
                    '{"issues":[{"code":"invalid_value","values":'
                    '["application","provenance"],"path":'
                    '["artifacts",6,"role"]}]}',
                ),
            )
        )


if __name__ == "__main__":
    unittest.main()
