#include <unity.h>

#include "FirmwareUpdateProtocol.h"

void setUp() {}
void tearDown() {}

namespace {

const char *VALID_RESPONSE = R"json({
  "protocolVersion": 1,
  "shouldUpdate": true,
  "updateAvailable": true,
  "reason": "update-available",
  "reportedTrack": "main",
  "assignedTrack": "staging",
  "trackChanged": true,
  "firmwareOrigin": "official",
  "currentProvenance": "current.jws.token",
  "currentVersion": "1.21.15",
  "targetVersion": "1.21.16",
  "nextHopVersion": "1.21.16",
  "update": {
    "provenance": "target.jws.token",
    "releaseId": "00000000-0000-4000-8000-000000000001",
    "buildSha": "0123456789abcdef",
    "kind": "firmware",
    "publishedAt": "2026-07-16T00:00:00.000Z",
    "artifacts": [
      {
        "role": "bootloader",
        "url": "https://example.supabase.co/storage/v1/object/public/lkbx-firmware/releases/1.21.16/0123456789abcdef/bootloader.bin",
        "sha256": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
        "sizeBytes": 512,
        "installOrder": 0,
        "required": false
      },
      {
        "role": "application",
        "url": "https://example.supabase.co/storage/v1/object/public/lkbx-firmware/releases/1.21.16/0123456789abcdef/firmware.bin",
        "sha256": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "sizeBytes": 1024,
        "installOrder": 1,
        "required": true
      }
    ]
  },
  "nextCheckSeconds": 60
})json";

void test_serializes_required_and_hardware_fields() {
    firmware::DeviceReport report{
        .deviceType = "lkbx",
        .deviceId = "trainer-test-id",
        .reportedTrack = "main",
        .currentVersion = "1.21.15",
        .currentBuild = "0123456789abcdef",
        .firmwareHash =
            "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        .provenanceCapability = 1,
        .firmwareProvenance = "current.jws.token",
        .chip = "esp32",
        .chipRevision = 0,
        .chipCores = 2,
        .hardwareRevision = "esp32-s3-n16r8",
        .flashSizeBytes = 4194304,
        .otaSlotSizeBytes = 3932160,
        .psramSizeBytes = 0,
        .partitionLayout = "legacy-v1",
    };
    JsonDocument parsed;
    deserializeJson(parsed, firmware::serializeReport(report));
    TEST_ASSERT_EQUAL_INT(firmware::PROTOCOL_VERSION,
                          parsed["protocolVersion"]);
    TEST_ASSERT_EQUAL_STRING("lkbx", parsed["deviceType"]);
    TEST_ASSERT_EQUAL_STRING("trainer-test-id", parsed["deviceId"]);
    TEST_ASSERT_EQUAL_STRING("main", parsed["reportedTrack"]);
    TEST_ASSERT_EQUAL_INT(1, parsed["provenanceCapability"]);
    TEST_ASSERT_EQUAL_STRING("current.jws.token",
                             parsed["firmwareProvenance"]);
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef", parsed["currentBuild"]);
    TEST_ASSERT_EQUAL_STRING(
        "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        parsed["firmwareHash"]);
    TEST_ASSERT_EQUAL_UINT32(0, parsed["chipRevision"]);
    TEST_ASSERT_EQUAL_UINT32(2, parsed["chipCores"]);
    TEST_ASSERT_EQUAL_UINT32(4194304, parsed["flashSizeBytes"]);
    TEST_ASSERT_EQUAL_UINT32(3932160, parsed["otaSlotSizeBytes"]);
    TEST_ASSERT_EQUAL_UINT32(0, parsed["psramSizeBytes"]);
    TEST_ASSERT_EQUAL_STRING("legacy-v1", parsed["partitionLayout"]);

    report.chipCores = 0;
    JsonDocument withoutCoreCount;
    deserializeJson(withoutCoreCount, firmware::serializeReport(report));
    TEST_ASSERT_TRUE(withoutCoreCount["chipCores"].isNull());
    TEST_ASSERT_EQUAL_UINT32(0, withoutCoreCount["chipRevision"]);
    TEST_ASSERT_EQUAL_UINT32(0, withoutCoreCount["psramSizeBytes"]);
}

void test_parses_cross_track_update_and_orders_artifacts() {
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_TRUE(
        firmware::parseDecision(VALID_RESPONSE, "lkbx", decision, error));
    TEST_ASSERT_EQUAL_INT(firmware::PROTOCOL_VERSION,
                          decision.protocolVersion);
    TEST_ASSERT_TRUE(decision.shouldUpdate);
    TEST_ASSERT_TRUE(decision.updateAvailable);
    TEST_ASSERT_EQUAL_STRING("update-available", decision.reason.c_str());
    TEST_ASSERT_EQUAL_STRING("staging", decision.assignedTrack.c_str());
    TEST_ASSERT_EQUAL_STRING("official", decision.firmwareOrigin.c_str());
    TEST_ASSERT_EQUAL_STRING("current.jws.token",
                             decision.currentProvenance.c_str());
    TEST_ASSERT_EQUAL_STRING("target.jws.token",
                             decision.provenance.c_str());
    TEST_ASSERT_TRUE(decision.trackChanged);
    TEST_ASSERT_EQUAL_STRING("1.21.16", decision.nextHopVersion.c_str());
    TEST_ASSERT_EQUAL_STRING("0123456789abcdef", decision.buildSha.c_str());
    TEST_ASSERT_EQUAL_UINT32(2, decision.artifactCount);
    TEST_ASSERT_EQUAL_STRING("bootloader", decision.artifacts[0].role.c_str());
    TEST_ASSERT_FALSE(decision.artifacts[0].required);
    TEST_ASSERT_EQUAL_STRING("application", decision.artifacts[1].role.c_str());
    TEST_ASSERT_TRUE(decision.artifacts[1].required);
    TEST_ASSERT_TRUE(firmware::validateInstallPlan(decision, error));

    firmware::DeviceReport report{
        .deviceType = "lkbx",
        .reportedTrack = "main",
        .currentVersion = "1.21.15",
        .currentBuild = "0123456789abcdef",
    };
    TEST_ASSERT_TRUE(firmware::decisionMatchesReport(report, decision));
}

void test_accepts_legacy_decision_field() {
    std::string payload = VALID_RESPONSE;
    const std::string canonicalLine = "  \"shouldUpdate\": true,\n";
    payload.erase(payload.find(canonicalLine), canonicalLine.size());
    const std::string protocolLine = "  \"protocolVersion\": 1,\n";
    payload.erase(payload.find(protocolLine), protocolLine.size());
    const std::string reasonLine =
        "  \"reason\": \"update-available\",\n";
    payload.erase(payload.find(reasonLine), reasonLine.size());

    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_TRUE(
        firmware::parseDecision(payload, "lkbx", decision, error));
    TEST_ASSERT_TRUE(decision.shouldUpdate);
    TEST_ASSERT_TRUE(decision.updateAvailable);
    TEST_ASSERT_TRUE(decision.reason.empty());
    TEST_ASSERT_EQUAL_INT(firmware::PROTOCOL_VERSION,
                          decision.protocolVersion);
}

void test_requires_build_sha_for_canonical_updates_only() {
    std::string missingBuild = VALID_RESPONSE;
    const std::string buildLine =
        "    \"buildSha\": \"0123456789abcdef\",\n";
    missingBuild.erase(missingBuild.find(buildLine), buildLine.size());

    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_FALSE(
        firmware::parseDecision(missingBuild, "lkbx", decision, error));
    TEST_ASSERT_EQUAL_STRING(
        "canonical update response is missing build SHA", error.c_str());

    std::string legacy = missingBuild;
    const std::string protocolLine = "  \"protocolVersion\": 1,\n";
    const std::string canonicalLine = "  \"shouldUpdate\": true,\n";
    const std::string reasonLine =
        "  \"reason\": \"update-available\",\n";
    legacy.erase(legacy.find(protocolLine), protocolLine.size());
    legacy.erase(legacy.find(canonicalLine), canonicalLine.size());
    legacy.erase(legacy.find(reasonLine), reasonLine.size());
    TEST_ASSERT_TRUE(firmware::parseDecision(legacy, "lkbx", decision, error));
    TEST_ASSERT_TRUE(decision.shouldUpdate);
    TEST_ASSERT_TRUE(decision.buildSha.empty());

    std::string invalidBuild = VALID_RESPONSE;
    const auto buildValue = invalidBuild.find("0123456789abcdef");
    invalidBuild.replace(buildValue, std::string("0123456789abcdef").size(),
                         "invalid");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(invalidBuild, "lkbx", decision, error));
    TEST_ASSERT_EQUAL_STRING("invalid update build identifier", error.c_str());
}

void test_rejects_conflicting_decisions_and_protocol_versions() {
    firmware::Decision decision;
    std::string error;
    std::string payload = VALID_RESPONSE;
    const auto legacy = payload.find("\"updateAvailable\": true");
    payload.replace(legacy, std::string("\"updateAvailable\": true").size(),
                    "\"updateAvailable\": false");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));
    TEST_ASSERT_EQUAL_STRING("conflicting update decision fields",
                             error.c_str());

    payload = VALID_RESPONSE;
    const auto protocol = payload.find("\"protocolVersion\": 1");
    payload.replace(protocol, std::string("\"protocolVersion\": 1").size(),
                    "\"protocolVersion\": 2");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));

    payload = VALID_RESPONSE;
    const auto reason = payload.find("\"reason\": \"update-available\"");
    payload.replace(reason,
                    std::string("\"reason\": \"update-available\"").size(),
                    "\"reason\": 42");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));

    payload = VALID_RESPONSE;
    const std::string reasonLine =
        "  \"reason\": \"update-available\",\n";
    payload.erase(payload.find(reasonLine), reasonLine.size());
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));

    payload = VALID_RESPONSE;
    const auto canonicalReason =
        payload.find("\"reason\": \"update-available\"");
    payload.replace(
        canonicalReason,
        std::string("\"reason\": \"update-available\"").size(),
        "\"reason\": \"already-current\"");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));
}

void test_rejects_same_build_on_same_track() {
    firmware::DeviceReport report{
        .deviceType = "lkbx",
        .reportedTrack = "staging",
        .currentVersion = "1.21.15",
        .currentBuild = "ABCDEF012345",
    };
    firmware::Decision decision;
    decision.shouldUpdate = true;
    decision.reportedTrack = "staging";
    decision.assignedTrack = "staging";
    decision.currentVersion = "1.21.15";
    decision.buildSha = "abcdef0123456789abcdef0123456789abcdef01";

    TEST_ASSERT_TRUE(firmware::buildIdentifiersMatch(report.currentBuild,
                                                     decision.buildSha));
    TEST_ASSERT_FALSE(firmware::decisionMatchesReport(report, decision));

    decision.buildSha = "1111111222222233333334444444555555566666";
    TEST_ASSERT_TRUE(firmware::decisionMatchesReport(report, decision));

    decision.reportedTrack = "staging";
    decision.assignedTrack = "main";
    decision.trackChanged = true;
    decision.buildSha = report.currentBuild;
    TEST_ASSERT_TRUE(firmware::decisionMatchesReport(report, decision));
}

void test_completed_attempt_requires_assigned_track() {
    TEST_ASSERT_TRUE(firmware::runningReleaseMatchesAttempt(
        "staging", "staging", "1.21.18", "1.21.18",
        "abcdef0123456789", "ABCDEF012345"));

    // A fallback to the old main image must not clear a staging attempt, even
    // when both track builds share the same version and source build SHA.
    TEST_ASSERT_FALSE(firmware::runningReleaseMatchesAttempt(
        "staging", "main", "1.21.18", "1.21.18",
        "abcdef0123456789", "ABCDEF012345"));

    // Legacy decisions may not contain a build identifier. Version fallback
    // is still safe only when the compiled track also matches.
    TEST_ASSERT_TRUE(firmware::runningReleaseMatchesAttempt(
        "staging", "staging", "1.21.18", "1.21.18", "", "unknown"));
    TEST_ASSERT_FALSE(firmware::runningReleaseMatchesAttempt(
        "staging", "main", "1.21.18", "1.21.18", "", "unknown"));
}

void test_parses_canonical_no_update_and_rejects_update_data() {
    const char *noUpdate = R"json({
      "protocolVersion": 1,
      "shouldUpdate": false,
      "updateAvailable": false,
      "reason": "already-current",
      "reportedTrack": "staging",
      "assignedTrack": "staging",
      "trackChanged": false,
      "currentVersion": "1.21.17",
      "targetVersion": "1.21.17",
      "nextHopVersion": null,
      "update": null,
      "nextCheckSeconds": 300
    })json";
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_TRUE(
        firmware::parseDecision(noUpdate, "lkbx", decision, error));
    TEST_ASSERT_FALSE(decision.shouldUpdate);
    TEST_ASSERT_EQUAL_STRING("already-current", decision.reason.c_str());

    std::string incompatible = noUpdate;
    const auto currentReason =
        incompatible.find("\"reason\": \"already-current\"");
    incompatible.replace(
        currentReason,
        std::string("\"reason\": \"already-current\"").size(),
        "\"reason\": \"incompatible-device\"");
    TEST_ASSERT_TRUE(
        firmware::parseDecision(incompatible, "lkbx", decision, error));
    TEST_ASSERT_EQUAL_STRING("incompatible-device", decision.reason.c_str());

    std::string invalid = noUpdate;
    const auto nextHop = invalid.find("\"nextHopVersion\": null");
    invalid.replace(nextHop, std::string("\"nextHopVersion\": null").size(),
                    "\"nextHopVersion\": \"1.21.18\"");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(invalid, "lkbx", decision, error));

    invalid = noUpdate;
    const auto invalidReason =
        invalid.find("\"reason\": \"already-current\"");
    invalid.replace(
        invalidReason,
        std::string("\"reason\": \"already-current\"").size(),
        "\"reason\": \"update-available\"");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(invalid, "lkbx", decision, error));
}

void test_optional_artifacts_are_skipped_but_required_roles_fail() {
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_TRUE(
        firmware::parseDecision(VALID_RESPONSE, "lkbx", decision, error));
    TEST_ASSERT_TRUE(firmware::validateInstallPlan(decision, error));

    decision.artifacts[0].required = true;
    TEST_ASSERT_FALSE(firmware::validateInstallPlan(decision, error));
    TEST_ASSERT_EQUAL_STRING("unsupported required artifact role: bootloader",
                             error.c_str());

    std::string payload = VALID_RESPONSE;
    const auto requiredApplication = payload.rfind("\"required\": true");
    payload.replace(requiredApplication,
                    std::string("\"required\": true").size(),
                    "\"required\": false");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));
}

void test_rejects_wrong_bucket_and_non_https_urls() {
    std::string payload = VALID_RESPONSE;
    const auto bucket = payload.find("lkbx-firmware");
    payload.replace(bucket, std::string("lkbx-firmware").size(), "dtt-firmware");
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));

    payload = VALID_RESPONSE;
    const auto scheme = payload.find("https://");
    payload.replace(scheme, std::string("https://").size(), "http://");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));

    payload = VALID_RESPONSE;
    const auto trustedHost = payload.find("example.supabase.co");
    payload.replace(trustedHost, std::string("example.supabase.co").size(),
                    "evil.example");
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));
}

void test_rejects_bad_hash_duplicate_order_and_missing_fields() {
    std::string payload = VALID_RESPONSE;
    const auto hash = payload.find(std::string(64, 'a'));
    payload.replace(hash, 64, "bad");
    firmware::Decision decision;
    std::string error;
    TEST_ASSERT_FALSE(
        firmware::parseDecision(payload, "lkbx", decision, error));
    TEST_ASSERT_FALSE(firmware::parseDecision("{}", "lkbx", decision, error));
}

}  // namespace

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_serializes_required_and_hardware_fields);
    RUN_TEST(test_parses_cross_track_update_and_orders_artifacts);
    RUN_TEST(test_accepts_legacy_decision_field);
    RUN_TEST(test_requires_build_sha_for_canonical_updates_only);
    RUN_TEST(test_rejects_conflicting_decisions_and_protocol_versions);
    RUN_TEST(test_rejects_same_build_on_same_track);
    RUN_TEST(test_completed_attempt_requires_assigned_track);
    RUN_TEST(test_parses_canonical_no_update_and_rejects_update_data);
    RUN_TEST(test_optional_artifacts_are_skipped_but_required_roles_fail);
    RUN_TEST(test_rejects_wrong_bucket_and_non_https_urls);
    RUN_TEST(test_rejects_bad_hash_duplicate_order_and_missing_fields);
    return UNITY_END();
}
