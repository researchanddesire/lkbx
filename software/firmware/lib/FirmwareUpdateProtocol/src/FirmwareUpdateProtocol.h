#pragma once

#include <ArduinoJson.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>

namespace firmware {

constexpr int PROTOCOL_VERSION = 1;
constexpr std::size_t MAX_ARTIFACTS = 8;

struct DeviceReport {
    int protocolVersion = PROTOCOL_VERSION;
    std::string deviceType;
    std::string deviceId;
    std::string reportedTrack;
    std::string currentVersion;
    std::string currentBuild;
    std::string firmwareHash;
    int provenanceCapability = 1;
    std::string firmwareProvenance;
    std::string chip;
    std::uint32_t chipRevision = 0;
    std::uint32_t chipCores = 0;
    std::string hardwareRevision;
    std::uint32_t flashSizeBytes = 0;
    std::uint32_t otaSlotSizeBytes = 0;
    std::uint32_t psramSizeBytes = 0;
    std::string partitionLayout;
};

struct Artifact {
    std::string role;
    std::string url;
    std::string sha256;
    std::uint32_t sizeBytes = 0;
    int installOrder = 0;
    bool required = true;
};

struct Decision {
    int protocolVersion = PROTOCOL_VERSION;
    bool shouldUpdate = false;
    // Kept as a compatibility mirror while deployed servers and devices move
    // from updateAvailable to the canonical shouldUpdate field.
    bool updateAvailable = false;
    std::string reason;
    std::string reportedTrack;
    std::string assignedTrack;
    bool trackChanged = false;
    std::string currentVersion;
    std::string targetVersion;
    std::string nextHopVersion;
    std::string releaseId;
    std::string buildSha;
    std::string firmwareOrigin;
    std::string currentProvenance;
    std::string provenance;
    std::string kind;
    std::array<Artifact, MAX_ARTIFACTS> artifacts{};
    std::size_t artifactCount = 0;
    std::uint32_t nextCheckSeconds = 0;
};

inline bool isTrack(const std::string &value) {
    return value == "main" || value == "staging";
}

inline bool isSemver(const std::string &value) {
    int dots = 0;
    bool digitInPart = false;
    for (const char character : value) {
        if (character == '.') {
            if (!digitInPart || ++dots > 2) return false;
            digitInPart = false;
        } else if (character == '+' || character == '-') {
            break;
        } else if (std::isdigit(static_cast<unsigned char>(character))) {
            digitInPart = true;
        } else {
            return false;
        }
    }
    return dots == 2 && digitInPart;
}

inline bool isSha256(const std::string &value) {
    return value.size() == 64 &&
           std::all_of(value.begin(), value.end(), [](const char character) {
               return std::isxdigit(static_cast<unsigned char>(character));
           });
}

inline bool isBuildIdentifier(const std::string &value) {
    return value.size() >= 7 && value.size() <= 64 &&
           std::all_of(value.begin(), value.end(), [](const char character) {
               return std::isxdigit(static_cast<unsigned char>(character));
           });
}

inline bool buildIdentifiersMatch(const std::string &left,
                                  const std::string &right) {
    if (!isBuildIdentifier(left) || !isBuildIdentifier(right)) return false;
    const auto sharedLength = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < sharedLength; ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

inline bool isArtifactRole(const std::string &role) {
    return role == "application" || role == "filesystem" ||
           role == "bootloader" || role == "partitions" ||
           role == "manifest";
}

inline bool isDecisionReason(const std::string &reason) {
    return reason == "update-available" || reason == "already-current" ||
           reason == "no-target" || reason == "updates-disabled" ||
           reason == "release-paused" || reason == "rollout-denied" ||
           reason == "incompatible-device";
}

// The Supabase Storage hosts the dashboard serves firmware from: production
// (main releases) and staging (staging releases). Both are trusted on every
// build, because the control plane can move a box to the other track and then
// offers that track's releases. Any other host, including any other
// *.supabase.co project, is refused.
inline constexpr const char *TRUSTED_ARTIFACT_HOSTS[] = {
    "acjajruwevyyatztbkdf.supabase.co",
    "meuaxbjzqrszxdvmacug.supabase.co",
};

inline bool isTrustedArtifactHost(const std::string &host) {
    for (const char *trusted : TRUSTED_ARTIFACT_HOSTS) {
        if (host == trusted) return true;
    }
    return false;
}

inline bool isHttpsArtifactUrl(const std::string &url,
                               const std::string &expectedBucket) {
    const std::string prefix = "https://";
    const std::string storagePath =
        "/storage/v1/object/public/" + expectedBucket +
        "/releases/";
    if (url.rfind(prefix, 0) != 0 || url.find('@') != std::string::npos ||
        url.find('#') != std::string::npos || url.find('?') != std::string::npos) {
        return false;
    }
    const auto hostEnd = url.find('/', prefix.size());
    if (hostEnd == std::string::npos) return false;
    const std::string host = url.substr(prefix.size(), hostEnd - prefix.size());
    if (!isTrustedArtifactHost(host)) return false;
    return url.compare(hostEnd, storagePath.size(), storagePath) == 0;
}

inline std::string serializeReport(const DeviceReport &report) {
    JsonDocument document;
    document["protocolVersion"] = report.protocolVersion;
    document["deviceType"] = report.deviceType;
    document["deviceId"] = report.deviceId;
    document["reportedTrack"] = report.reportedTrack;
    document["currentVersion"] = report.currentVersion;
    if (!report.currentBuild.empty())
        document["currentBuild"] = report.currentBuild;
    if (!report.firmwareHash.empty())
        document["firmwareHash"] = report.firmwareHash;
    document["provenanceCapability"] = report.provenanceCapability;
    if (!report.firmwareProvenance.empty())
        document["firmwareProvenance"] = report.firmwareProvenance;
    if (!report.chip.empty()) document["chip"] = report.chip;
    document["chipRevision"] = report.chipRevision;
    if (report.chipCores > 0) document["chipCores"] = report.chipCores;
    if (!report.hardwareRevision.empty())
        document["hardwareRevision"] = report.hardwareRevision;
    if (report.flashSizeBytes > 0)
        document["flashSizeBytes"] = report.flashSizeBytes;
    if (report.otaSlotSizeBytes > 0)
        document["otaSlotSizeBytes"] = report.otaSlotSizeBytes;
    document["psramSizeBytes"] = report.psramSizeBytes;
    if (!report.partitionLayout.empty())
        document["partitionLayout"] = report.partitionLayout;
    std::string output;
    serializeJson(document, output);
    return output;
}

inline bool readRequiredString(JsonVariantConst value, std::string &output) {
    if (!value.is<const char *>()) return false;
    output = value.as<const char *>();
    return !output.empty();
}

inline bool readNullableVersion(JsonVariantConst value, std::string &output) {
    if (value.isNull()) {
        output.clear();
        return true;
    }
    return readRequiredString(value, output) && isSemver(output);
}

inline bool parseDecision(const std::string &payload,
                          const std::string &expectedDeviceType,
                          Decision &decision, std::string &error) {
    JsonDocument document;
    const auto jsonError = deserializeJson(document, payload);
    if (jsonError) {
        error = "invalid JSON";
        return false;
    }

    Decision parsed;
    if ((!document["protocolVersion"].isNull() &&
         (!document["protocolVersion"].is<int>() ||
          document["protocolVersion"].as<int>() != PROTOCOL_VERSION)) ||
        (!document["shouldUpdate"].is<bool>() &&
         !document["updateAvailable"].is<bool>()) ||
        !document["trackChanged"].is<bool>() ||
        !document["nextCheckSeconds"].is<std::uint32_t>() ||
        !readRequiredString(document["reportedTrack"], parsed.reportedTrack) ||
        !readRequiredString(document["assignedTrack"], parsed.assignedTrack) ||
        !readRequiredString(document["currentVersion"], parsed.currentVersion) ||
        !readNullableVersion(document["targetVersion"], parsed.targetVersion) ||
        !readNullableVersion(document["nextHopVersion"], parsed.nextHopVersion) ||
        !isTrack(parsed.reportedTrack) || !isTrack(parsed.assignedTrack) ||
        !isSemver(parsed.currentVersion)) {
        error = "missing or invalid response fields";
        return false;
    }

    if (!document["protocolVersion"].isNull()) {
        parsed.protocolVersion = document["protocolVersion"].as<int>();
    }
    const bool hasCanonicalDecision = document["shouldUpdate"].is<bool>();
    const bool hasLegacyDecision = document["updateAvailable"].is<bool>();
    const bool canonicalResponse =
        !document["protocolVersion"].isNull() || hasCanonicalDecision ||
        !document["reason"].isNull();
    if (hasCanonicalDecision && hasLegacyDecision &&
        document["shouldUpdate"].as<bool>() !=
            document["updateAvailable"].as<bool>()) {
        error = "conflicting update decision fields";
        return false;
    }
    parsed.shouldUpdate = hasCanonicalDecision
                              ? document["shouldUpdate"].as<bool>()
                              : document["updateAvailable"].as<bool>();
    parsed.updateAvailable = parsed.shouldUpdate;
    if (hasCanonicalDecision) {
        if (!readRequiredString(document["reason"], parsed.reason) ||
            !isDecisionReason(parsed.reason) ||
            parsed.shouldUpdate != (parsed.reason == "update-available")) {
            error = "invalid canonical update reason";
            return false;
        }
    } else if (!document["reason"].isNull() &&
               !readRequiredString(document["reason"], parsed.reason)) {
        error = "invalid legacy update reason";
        return false;
    }
    parsed.trackChanged = document["trackChanged"].as<bool>();
    parsed.nextCheckSeconds = document["nextCheckSeconds"].as<std::uint32_t>();
    if (!document["firmwareOrigin"].isNull() &&
        !readRequiredString(document["firmwareOrigin"], parsed.firmwareOrigin)) {
        error = "invalid firmware origin";
        return false;
    }
    if (!document["currentProvenance"].isNull() &&
        !readRequiredString(document["currentProvenance"], parsed.currentProvenance)) {
        error = "invalid current firmware provenance";
        return false;
    }
    if (!parsed.shouldUpdate) {
        if (!document["update"].isNull() || !parsed.nextHopVersion.empty()) {
            error = "no-update response contains update data";
            return false;
        }
        decision = parsed;
        return true;
    }

    JsonObjectConst update = document["update"].as<JsonObjectConst>();
    JsonArrayConst artifacts = update["artifacts"].as<JsonArrayConst>();
    if (update.isNull() || artifacts.isNull() || artifacts.size() == 0 ||
        artifacts.size() > MAX_ARTIFACTS ||
        !readRequiredString(update["releaseId"], parsed.releaseId) ||
        !readRequiredString(update["kind"], parsed.kind) ||
        (parsed.kind != "firmware" && parsed.kind != "migration") ||
        parsed.nextHopVersion.empty()) {
        error = "invalid update metadata";
        return false;
    }
    if (!update["buildSha"].isNull() &&
        (!readRequiredString(update["buildSha"], parsed.buildSha) ||
         !isBuildIdentifier(parsed.buildSha))) {
        error = "invalid update build identifier";
        return false;
    } else if (canonicalResponse && parsed.buildSha.empty()) {
        error = "canonical update response is missing build SHA";
        return false;
    }
    if (!update["provenance"].isNull() &&
        !readRequiredString(update["provenance"], parsed.provenance)) {
        error = "invalid update firmware provenance";
        return false;
    }

    const std::string expectedBucket = expectedDeviceType + "-firmware";
    std::array<bool, MAX_ARTIFACTS> seenOrders{};
    std::size_t requiredApplicationCount = 0;
    for (JsonObjectConst item : artifacts) {
        Artifact artifact;
        if (!readRequiredString(item["role"], artifact.role) ||
            !readRequiredString(item["url"], artifact.url) ||
            !readRequiredString(item["sha256"], artifact.sha256) ||
            !item["sizeBytes"].is<std::uint32_t>() ||
            !item["installOrder"].is<int>() ||
            (!item["required"].isNull() && !item["required"].is<bool>()) ||
            !isArtifactRole(artifact.role) || !isSha256(artifact.sha256) ||
            !isHttpsArtifactUrl(artifact.url, expectedBucket)) {
            error = "invalid artifact";
            return false;
        }
        artifact.sizeBytes = item["sizeBytes"].as<std::uint32_t>();
        artifact.installOrder = item["installOrder"].as<int>();
        if (!item["required"].isNull()) {
            artifact.required = item["required"].as<bool>();
        }
        if (artifact.sizeBytes == 0 || artifact.installOrder < 0 ||
            artifact.installOrder >= static_cast<int>(MAX_ARTIFACTS) ||
            seenOrders[artifact.installOrder]) {
            error = "invalid artifact size or order";
            return false;
        }
        if (artifact.required && artifact.role == "application") {
            ++requiredApplicationCount;
        }
        seenOrders[artifact.installOrder] = true;
        parsed.artifacts[parsed.artifactCount++] = artifact;
    }
    if (requiredApplicationCount != 1) {
        error = "release must contain exactly one required application artifact";
        return false;
    }

    std::sort(parsed.artifacts.begin(),
              parsed.artifacts.begin() + parsed.artifactCount,
              [](const Artifact &left, const Artifact &right) {
                  return left.installOrder < right.installOrder;
              });
    decision = parsed;
    return true;
}

inline bool validateInstallPlan(const Decision &decision, std::string &error) {
    std::size_t requiredApplicationCount = 0;
    for (std::size_t index = 0; index < decision.artifactCount; ++index) {
        const Artifact &artifact = decision.artifacts[index];
        if (!artifact.required) continue;
        if (artifact.role == "application") {
            ++requiredApplicationCount;
        } else if (artifact.role != "filesystem") {
            error = "unsupported required artifact role: " + artifact.role;
            return false;
        }
    }
    if (requiredApplicationCount != 1) {
        error = "release must contain exactly one required application artifact";
        return false;
    }
    return true;
}

inline bool decisionMatchesReport(const DeviceReport &report,
                                  const Decision &decision) {
    if (decision.reportedTrack != report.reportedTrack ||
        decision.currentVersion != report.currentVersion ||
        decision.trackChanged !=
            (decision.reportedTrack != decision.assignedTrack)) {
        return false;
    }
    return !decision.shouldUpdate ||
           decision.assignedTrack != report.reportedTrack ||
           !buildIdentifiersMatch(report.currentBuild, decision.buildSha);
}

inline bool runningReleaseMatchesAttempt(
    const std::string &expectedTrack, const std::string &runningTrack,
    const std::string &expectedVersion, const std::string &runningVersion,
    const std::string &expectedBuild, const std::string &runningBuild) {
    if (expectedTrack.empty() || expectedTrack != runningTrack) return false;
    if (!expectedBuild.empty()) {
        return buildIdentifiersMatch(expectedBuild, runningBuild);
    }
    return !expectedVersion.empty() && expectedVersion == runningVersion;
}

}  // namespace firmware
