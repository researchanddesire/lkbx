#pragma once

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

namespace firmware::provenance {

// These are result-body bounds, leaving space for the unchanged RAD BLE
// terminal envelope and both machine-state names. Callers test the real sender.
constexpr std::size_t PROVENANCE_RESULT_BYTES = 304;
constexpr std::size_t PROVENANCE_PROOF_BYTES = 4096;
constexpr std::size_t PROVENANCE_PAGE_BYTES = 64;

struct Response {
    bool ok;
    const char* code;
    const char* message;
    std::string json;
};

inline Response responseFailure(const char* code, const char* message) {
    return {false, code, message, "{}"};
}

inline bool proofReferenceCharacter(unsigned char value) {
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z') ||
           (value >= '0' && value <= '9') || value == '-' || value == '_';
}

inline std::string encodeProofChunk(const std::string& token,
                                    std::size_t offset, std::size_t count) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string encoded;
    encoded.reserve((count * 8 + 5) / 6);
    unsigned buffer = 0;
    int bits = 0;
    for (std::size_t index = offset; index < offset + count; ++index) {
        buffer = (buffer << 8) | static_cast<unsigned char>(token[index]);
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            encoded.push_back(alphabet[(buffer >> bits) & 63]);
        }
    }
    if (bits) encoded.push_back(alphabet[(buffer << (6 - bits)) & 63]);
    return encoded;
}

// Snapshot classification/signature verification remains in FirmwareProvenance.
// A summary is metadata, not a replacement for verifying the complete signed
// token and matching its runtime-image claims on the consuming client.
template <typename Snapshot>
Response response(const Snapshot& snapshot, JsonObjectConst args) {
    const auto invalid = []() {
        return responseFailure("invalid_value", "Invalid provenance request");
    };
    std::string view = "summary";
    if (!args["view"].isNull()) {
        if (!args["view"].is<const char*>()) return invalid();
        const JsonString value = args["view"].as<JsonString>();
        if (value.size() != std::strlen(value.c_str())) return invalid();
        view.assign(value.c_str(), value.size());
    }
    if (view != "summary" && view != "proof" && view != "full") return invalid();
    for (JsonPairConst field : args) {
        const char* key = field.key().c_str();
        if (field.key().size() != std::strlen(key)) return invalid();
        const bool allowed = std::strcmp(key, "view") == 0 ||
            (view != "summary" && std::strcmp(key, "provenanceId") == 0) ||
            (view == "proof" && std::strcmp(key, "offset") == 0);
        if (!allowed) return invalid();
    }
    if ((snapshot.origin != "official" && snapshot.origin != "community" &&
         snapshot.origin != "invalid") || snapshot.keyId.size() > 32 ||
        (!snapshot.provenanceId.empty() && snapshot.provenanceId.size() != 43) ||
        (!snapshot.imageSha256.empty() && snapshot.imageSha256.size() != 64) ||
        (!snapshot.token.empty() && snapshot.provenanceId.size() != 43)) {
        return responseFailure("provenance_unavailable", "Provenance metadata unavailable");
    }
    for (unsigned char value : snapshot.provenanceId) {
        if (!proofReferenceCharacter(value)) {
            return responseFailure("provenance_unavailable", "Provenance metadata unavailable");
        }
    }
    for (unsigned char value : snapshot.imageSha256) {
        if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            return responseFailure("provenance_unavailable", "Provenance metadata unavailable");
        }
    }
    for (unsigned char value : snapshot.keyId) {
        if (!proofReferenceCharacter(value)) {
            return responseFailure("provenance_unavailable", "Provenance metadata unavailable");
        }
    }
    if (snapshot.token.size() > PROVENANCE_PROOF_BYTES) {
        return responseFailure("proof_too_large", "Provenance proof exceeds supported bounds");
    }
    JsonDocument document;
    document["path"] = "device.firmwareProvenance";
    document["v"] = 2;
    if (view == "summary" || view == "full") {
        document["origin"] = snapshot.origin;
        document["keyId"] = snapshot.keyId;
        document["provenanceId"] = snapshot.provenanceId;
        document["imageSha256"] = snapshot.imageSha256;
        document["compactJws"] = "";
        document["proofBytes"] = static_cast<std::uint32_t>(snapshot.token.size());
        document["proofPageBytes"] = PROVENANCE_PAGE_BYTES;
    }
    if (view != "summary") {
        if (!args["provenanceId"].is<const char*>()) return invalid();
        const JsonString reference = args["provenanceId"].as<JsonString>();
        if (reference.size() != snapshot.provenanceId.size() ||
            std::memcmp(reference.c_str(), snapshot.provenanceId.data(), reference.size()) != 0) {
            return responseFailure("stale_provenance", "Provenance changed; read summary again");
        }
        if (view == "proof") {
            if (!args["offset"].is<std::uint32_t>()) return invalid();
            const auto offset = args["offset"].as<std::uint32_t>();
            if (offset > snapshot.token.size()) return invalid();
            const auto count = std::min(PROVENANCE_PAGE_BYTES, snapshot.token.size() - offset);
            document["provenanceId"] = snapshot.provenanceId;
            document["offset"] = offset;
            document["totalBytes"] = snapshot.token.size();
            document["encoding"] = "base64url";
            document["chunk"] = encodeProofChunk(snapshot.token, offset, count);
            document["nextOffset"] = offset + count;
        } else {
            for (unsigned char value : snapshot.token) {
                if (!proofReferenceCharacter(value) && value != '.') {
                    return responseFailure("proof_requires_paging", "Read proof using bounded pages");
                }
            }
            document["compactJws"] = snapshot.token;
        }
    }
    std::string output;
    serializeJson(document, output);
    if (document.overflowed() || output.size() > PROVENANCE_RESULT_BYTES) {
        return responseFailure(view == "full" ? "proof_requires_paging" : "provenance_unavailable",
                               view == "full" ? "Read proof using bounded pages" : "Provenance metadata unavailable");
    }
    return {true, "", "", std::move(output)};
}

}  // namespace firmware::provenance
