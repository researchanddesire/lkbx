#!/usr/bin/env python3

import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import download_verified_release as downloader
from download_verified_release import (
    legacy_version_document,
    local_provenance_document,
)


class LegacyVersionDocumentTests(unittest.TestCase):
    def test_includes_legacy_numeric_fields_and_immutable_identity(self) -> None:
        self.assertEqual(
            legacy_version_document("1.21.18", "abc123", "release-id", "r2"),
            {
                "version": "1.21.18",
                "major": 1,
                "minor": 21,
                "patch": 18,
                "buildSha": "abc123",
                "releaseId": "release-id",
                "hardwareVariant": "r2",
            },
        )

    def test_rejects_non_numeric_semver(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "numeric SemVer"):
            legacy_version_document(
                "1.21.18-beta.1", "abc123", "release-id", "r8"
            )

    def test_local_provenance_document_binds_release_id_canonically(self) -> None:
        self.assertEqual(
            local_provenance_document(
                "signed.token.value",
                "00000000-0000-4000-8000-000000000019",
            ),
            '{"provenance":"signed.token.value","releaseId":'
            '"00000000-0000-4000-8000-000000000019"}\n',
        )

    def test_rejects_release_for_another_hardware_variant(self) -> None:
        status = {
            "track": "staging",
            "deviceType": "lkbx",
            "hardwareVariant": "r8",
            "kind": "firmware",
            "lifecycle": "active",
        }
        with tempfile.TemporaryDirectory() as directory:
            with (
                mock.patch.dict(
                    os.environ, {"FIRMWARE_PUBLISH_TOKEN": "test-token"}
                ),
                mock.patch.object(
                    downloader, "fetch", return_value=json.dumps(status).encode()
                ),
            ):
                with self.assertRaisesRegex(RuntimeError, "hardware variant"):
                    downloader.download_release(
                        "release-id",
                        "staging",
                        "lkbx",
                        "r2",
                        Path(directory),
                    )

    def test_bridge_download_requires_the_migration_kind(self) -> None:
        status = {
            "track": "staging",
            "deviceType": "lkbx",
            "hardwareVariant": "default",
            "kind": "firmware",
            "lifecycle": "ready",
        }
        with tempfile.TemporaryDirectory() as directory:
            with (
                mock.patch.dict(
                    os.environ, {"FIRMWARE_PUBLISH_TOKEN": "test-token"}
                ),
                mock.patch.object(
                    downloader, "fetch", return_value=json.dumps(status).encode()
                ),
            ):
                with self.assertRaisesRegex(RuntimeError, "kind"):
                    downloader.download_release(
                        "release-id",
                        "staging",
                        "lkbx",
                        "default",
                        Path(directory),
                        "migration",
                    )


if __name__ == "__main__":
    unittest.main()
