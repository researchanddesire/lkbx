#pragma once

#include <ArduinoJson.h>

#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Base64Url.h"
#include "CloudInput.h"
#include "CommandCrypto.h"

namespace lockbox::security {

constexpr std::size_t COMMAND_KEY_BYTES = 32;
constexpr std::size_t COMMAND_IV_BYTES = 16;
constexpr std::size_t COMMAND_MAX_TEXT = 8192;
// pg_net does not guarantee delivery order, so an older command is accepted
// for a short window and exact replays are caught by the signature ring.
constexpr std::uint64_t COMMAND_REORDER_WINDOW_MS = 30000;
constexpr std::size_t COMMAND_REPLAY_RING = 8;
constexpr std::size_t COMMAND_REPLAY_PREFIX = 8;
constexpr std::uint64_t COMMAND_MAX_TIMESTAMP = 9007199254740991ULL;
// Commands arrive within seconds; the broker keeps no queue for the box. Once
// the clock is server-verified, anything older than this is a recording.
constexpr std::uint64_t COMMAND_MAX_AGE_MS = 10ULL * 60 * 1000;
// What survives a reboot: the high-water timestamp (8 bytes, little-endian),
// the ring's fill count and next slot, then the ring itself.
constexpr std::size_t COMMAND_REPLAY_STATE_BYTES =
    8 + 2 + COMMAND_REPLAY_RING * COMMAND_REPLAY_PREFIX;
constexpr std::size_t REPORT_NONCE_BYTES = 16;
// A sealed last will carries a plausible clock or the dashboard refuses it:
// 2024-01-01T00:00:00Z, in milliseconds.
constexpr std::uint64_t LAST_WILL_EARLIEST_MS = 1704067200000ULL;

enum class CommandGateResult {
    plain,          // No key is held: the frame is today's plain JSON.
    opened,         // A verified and decrypted envelope.
    malformed,
    badSignature,
    stale,
    replayed,
    decryptFailed,
    unsignedRefused,  // A box with a burned key never takes plain JSON.
};

// Who an envelope is for. A v1 sign-in binds v2 envelopes and r2 reports to
// the upper-case MAC; a v2 sign-in binds v3 envelopes and r3 reports to the
// eFuse device id, used verbatim.
enum class EnvelopeBinding { mac, deviceId };

// A sealed report names its kind, so a log can never be replayed as a state
// report or the other way round.
enum class ReportKind { state, log };

inline const char* reportKindText(ReportKind kind) {
    return kind == ReportKind::log ? "log" : "state";
}

// The report inside a sealed last will. The dashboard records the disconnect
// under the will's nonce, so the report carries the same text as its id.
// `createdAt` is 0: the box cannot know when the connection will drop.
inline std::string lastWillReport(const std::string& id,
                                  const char* firmwareProvenanceId) {
    JsonDocument document;
    document["id"] = id;
    document["state"] = "disconnected";
    document["createdAt"] = 0;
    document["firmwareProvenanceId"] =
        firmwareProvenanceId ? firmwareProvenanceId : "";
    std::string report;
    serializeJson(document, report);
    return report;
}

inline bool commandGateAccepted(CommandGateResult result) {
    return result == CommandGateResult::plain ||
           result == CommandGateResult::opened;
}

inline bool constantTimeEqual(const std::uint8_t* left, const std::uint8_t* right,
                              std::size_t length) {
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < length; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0;
}

// Holding a key means every command must be a valid envelope. Holding none
// leaves unenrolled behaviour unchanged, unless signatures are required: a box
// with a burned eFuse key refuses plain JSON even before it holds a key. The
// server only hands a key to a device it signs for.
class CommandGate {
 public:
    bool hasKey() const { return hasKey_; }
    std::uint64_t lastTimestamp() const { return lastTimestamp_; }
    void restoreLastTimestamp(std::uint64_t timestamp) {
        lastTimestamp_ = timestamp;
    }
    void requireSignatures(bool required) { signaturesRequired_ = required; }
    bool signaturesRequired() const { return signaturesRequired_; }

    // The replay ring and high-water timestamp, so a reboot or a new sign-in
    // cannot make an already accepted command acceptable again.
    void exportReplayState(std::uint8_t (&out)[COMMAND_REPLAY_STATE_BYTES]) const {
        for (std::size_t index = 0; index < 8; ++index) {
            out[index] = static_cast<std::uint8_t>(lastTimestamp_ >> (8 * index));
        }
        out[8] = static_cast<std::uint8_t>(ringCount_);
        out[9] = static_cast<std::uint8_t>(ringNext_);
        for (std::size_t slot = 0; slot < COMMAND_REPLAY_RING; ++slot) {
            std::memcpy(out + 10 + slot * COMMAND_REPLAY_PREFIX, ring_[slot].data(),
                        COMMAND_REPLAY_PREFIX);
        }
    }

    bool restoreReplayState(const std::uint8_t* data, std::size_t length) {
        if (!data || length != COMMAND_REPLAY_STATE_BYTES ||
            data[8] > COMMAND_REPLAY_RING || data[9] >= COMMAND_REPLAY_RING) return false;
        std::uint64_t timestamp = 0;
        for (std::size_t index = 0; index < 8; ++index) {
            timestamp |= static_cast<std::uint64_t>(data[index]) << (8 * index);
        }
        if (timestamp > COMMAND_MAX_TIMESTAMP) return false;
        lastTimestamp_ = timestamp;
        ringCount_ = data[8];
        ringNext_ = data[9];
        for (std::size_t slot = 0; slot < COMMAND_REPLAY_RING; ++slot) {
            std::memcpy(ring_[slot].data(), data + 10 + slot * COMMAND_REPLAY_PREFIX,
                        COMMAND_REPLAY_PREFIX);
        }
        return true;
    }

    bool setKey(const char* text, std::size_t length) {
        std::uint8_t key[COMMAND_KEY_BYTES];
        std::size_t written = 0;
        if (!text || !base64UrlDecode(text, length, key, sizeof(key), written) ||
            written != sizeof(key)) return false;
        static const char macLabel[] = "lkbx-cmd-mac-v2";
        static const char encLabel[] = "lkbx-cmd-enc-v2";
        static const char reportMacLabel[] = "lkbx-rpt-mac-v2";
        static const char reportEncLabel[] = "lkbx-rpt-enc-v2";
        std::uint8_t macKey[HMAC_SHA256_BYTES], encKey[HMAC_SHA256_BYTES];
        std::uint8_t reportMacKey[HMAC_SHA256_BYTES], reportEncKey[HMAC_SHA256_BYTES];
        const auto derive = [&key](const char* label, std::size_t length,
                                   std::uint8_t* out) {
            return hmacSha256(key, sizeof(key),
                              reinterpret_cast<const std::uint8_t*>(label), length, out);
        };
        if (!derive(macLabel, sizeof(macLabel) - 1, macKey) ||
            !derive(encLabel, sizeof(encLabel) - 1, encKey) ||
            !derive(reportMacLabel, sizeof(reportMacLabel) - 1, reportMacKey) ||
            !derive(reportEncLabel, sizeof(reportEncLabel) - 1, reportEncKey)) return false;
        std::memcpy(macKey_, macKey, sizeof(macKey_));
        std::memcpy(encKey_, encKey, sizeof(encKey_));
        std::memcpy(reportMacKey_, reportMacKey, sizeof(reportMacKey_));
        std::memcpy(reportEncKey_, reportEncKey, sizeof(reportEncKey_));
        // The replay ring stays: a sign-in returns the same key, and the
        // commands it already accepted must stay spent.
        hasKey_ = true;
        return true;
    }

    void clearKey() {
        std::memset(macKey_, 0, sizeof(macKey_));
        std::memset(encKey_, 0, sizeof(encKey_));
        std::memset(reportMacKey_, 0, sizeof(reportMacKey_));
        std::memset(reportEncKey_, 0, sizeof(reportEncKey_));
        hasKey_ = false;
    }

    CommandGateResult open(const char* mac, const char* frame, std::size_t size,
                           std::string& command) {
        return open(mac, EnvelopeBinding::mac, frame, size, command);
    }

    // `trustedNowMs` is the server-verified wall clock, or 0 while the clock
    // is not verified (only the reorder window and the ring apply then).
    CommandGateResult open(const char* recipient, EnvelopeBinding binding,
                           const char* frame, std::size_t size, std::string& command,
                           std::uint64_t trustedNowMs = 0) {
        command.clear();
        if (!hasKey_) {
            if (signaturesRequired_) return CommandGateResult::unsignedRefused;
            command.assign(frame, size);
            return CommandGateResult::plain;
        }
        const int version = binding == EnvelopeBinding::mac ? 2 : 3;
        JsonDocument document;
        if (!recipient || !*recipient || deserializeJson(document, frame, size)) {
            return CommandGateResult::malformed;
        }
        const JsonObjectConst envelope = document.as<JsonObjectConst>();
        if (envelope.isNull() || envelope.size() != 5 ||
            !envelope["v"].is<int>() || envelope["v"].as<int>() != version ||
            !envelope["ts"].is<std::uint64_t>() ||
            !boundedString(envelope["iv"], 1, COMMAND_MAX_TEXT) ||
            !boundedString(envelope["ct"], 1, COMMAND_MAX_TEXT) ||
            !boundedString(envelope["sig"], 1, COMMAND_MAX_TEXT)) {
            return CommandGateResult::malformed;
        }
        const std::uint64_t timestamp = envelope["ts"].as<std::uint64_t>();
        const JsonString ivText = envelope["iv"].as<JsonString>();
        const JsonString cipherText = envelope["ct"].as<JsonString>();
        const JsonString signatureText = envelope["sig"].as<JsonString>();
        if (timestamp == 0 || timestamp > COMMAND_MAX_TIMESTAMP) {
            return CommandGateResult::malformed;
        }

        std::uint8_t iv[COMMAND_IV_BYTES], signature[HMAC_SHA256_BYTES];
        std::size_t ivLength = 0, signatureLength = 0, cipherLength = 0;
        std::vector<std::uint8_t> cipher(
            base64UrlDecodedLength(cipherText.size()));
        if (!base64UrlDecode(ivText.c_str(), ivText.size(), iv, sizeof(iv), ivLength) ||
            ivLength != sizeof(iv) ||
            !base64UrlDecode(signatureText.c_str(), signatureText.size(), signature,
                             sizeof(signature), signatureLength) ||
            signatureLength != sizeof(signature) ||
            !base64UrlDecode(cipherText.c_str(), cipherText.size(), cipher.data(),
                             cipher.size(), cipherLength) ||
            cipherLength == 0 || cipherLength % COMMAND_AES_BLOCK != 0) {
            return CommandGateResult::malformed;
        }

        // Encrypt-then-MAC: nothing is decrypted until the signature verifies.
        std::string signedText = version == 2 ? "v2." : "v3.";
        signedText += std::to_string(timestamp);
        signedText += '.';
        for (const char* cursor = recipient; *cursor; ++cursor) {
            signedText += binding == EnvelopeBinding::mac
                              ? static_cast<char>(std::toupper(
                                    static_cast<unsigned char>(*cursor)))
                              : *cursor;
        }
        signedText += '.';
        signedText.append(ivText.c_str(), ivText.size());
        signedText += '.';
        signedText.append(cipherText.c_str(), cipherText.size());
        std::uint8_t expected[HMAC_SHA256_BYTES];
        if (!hmacSha256(macKey_, sizeof(macKey_),
                        reinterpret_cast<const std::uint8_t*>(signedText.data()),
                        signedText.size(), expected) ||
            !constantTimeEqual(expected, signature, sizeof(expected))) {
            return CommandGateResult::badSignature;
        }

        if (timestamp + COMMAND_REORDER_WINDOW_MS < lastTimestamp_ ||
            (trustedNowMs != 0 && timestamp + COMMAND_MAX_AGE_MS < trustedNowMs)) {
            return CommandGateResult::stale;
        }
        for (std::size_t index = 0; index < ringCount_; ++index) {
            if (std::memcmp(ring_[index].data(), signature, COMMAND_REPLAY_PREFIX) == 0) {
                return CommandGateResult::replayed;
            }
        }

        std::vector<std::uint8_t> plain(cipherLength);
        if (!aes256CbcDecrypt(encKey_, iv, cipher.data(), cipherLength, plain.data())) {
            return CommandGateResult::decryptFailed;
        }
        const std::uint8_t padding = plain[cipherLength - 1];
        if (padding == 0 || padding > COMMAND_AES_BLOCK || padding > cipherLength) {
            return CommandGateResult::decryptFailed;
        }
        for (std::size_t index = cipherLength - padding; index < cipherLength; ++index) {
            if (plain[index] != padding) return CommandGateResult::decryptFailed;
        }
        const std::size_t plainLength = cipherLength - padding;
        if (plainLength == 0 || std::memchr(plain.data(), '\0', plainLength)) {
            return CommandGateResult::decryptFailed;
        }

        std::memcpy(ring_[ringNext_].data(), signature, COMMAND_REPLAY_PREFIX);
        ringNext_ = (ringNext_ + 1) % COMMAND_REPLAY_RING;
        if (ringCount_ < COMMAND_REPLAY_RING) ++ringCount_;
        if (timestamp > lastTimestamp_) lastTimestamp_ = timestamp;
        command.assign(reinterpret_cast<const char*>(plain.data()), plainLength);
        return CommandGateResult::opened;
    }

    // Seals a report to the dashboard in the command envelope's shape. Its own
    // keys and an "r2."/"r3." signature prefix mean a captured report can never
    // be opened, or replayed, as a command. `iv` must be fresh random bytes.
    bool seal(const char* mac, const std::string& report, std::uint64_t timestamp,
              const std::uint8_t (&iv)[COMMAND_IV_BYTES], std::string& envelope) const {
        static const std::uint8_t unused[REPORT_NONCE_BYTES] = {};
        return seal(mac, EnvelopeBinding::mac, ReportKind::state, report, timestamp,
                    iv, unused, envelope);
    }

    // A device-id binding seals v3: the kind and a fresh random `nonce` are
    // signed too, so the dashboard can refuse a report on the wrong route and
    // one it has already seen.
    bool seal(const char* recipient, EnvelopeBinding binding, ReportKind kind,
              const std::string& report, std::uint64_t timestamp,
              const std::uint8_t (&iv)[COMMAND_IV_BYTES],
              const std::uint8_t (&nonce)[REPORT_NONCE_BYTES],
              std::string& envelope) const {
        envelope.clear();
        if (!hasKey_ || !recipient || !*recipient || report.empty() ||
            report.size() > COMMAND_MAX_TEXT || timestamp == 0 ||
            timestamp > COMMAND_MAX_TIMESTAMP) return false;
        const std::size_t padding = COMMAND_AES_BLOCK - report.size() % COMMAND_AES_BLOCK;
        std::vector<std::uint8_t> plain(report.begin(), report.end());
        plain.insert(plain.end(), padding, static_cast<std::uint8_t>(padding));
        std::vector<std::uint8_t> cipher(plain.size());
        if (!aes256CbcEncrypt(reportEncKey_, iv, plain.data(), plain.size(),
                              cipher.data())) return false;
        const std::string ivText = base64UrlEncode(iv, sizeof(iv));
        const std::string cipherText = base64UrlEncode(cipher.data(), cipher.size());
        const bool v3 = binding == EnvelopeBinding::deviceId;
        const std::string nonceText = v3 ? base64UrlEncode(nonce, sizeof(nonce)) : "";
        std::string message = (v3 ? "r3." : "r2.") + std::to_string(timestamp) + "." +
                              recipient + ".";
        if (v3) message += std::string(reportKindText(kind)) + "." + nonceText + ".";
        message += ivText + "." + cipherText;
        std::uint8_t signature[HMAC_SHA256_BYTES];
        if (!hmacSha256(reportMacKey_, sizeof(reportMacKey_),
                        reinterpret_cast<const std::uint8_t*>(message.data()),
                        message.size(), signature)) return false;
        JsonDocument document;
        document["v"] = v3 ? 3 : 2;
        document["ts"] = timestamp;
        if (v3) {
            document["k"] = reportKindText(kind);
            document["n"] = nonceText;
        }
        document["iv"] = ivText;
        document["ct"] = cipherText;
        document["sig"] = base64UrlEncode(signature, sizeof(signature));
        serializeJson(document, envelope);
        return true;
    }

    // Seals the last will the broker publishes when a connection drops:
    //
    //   {"v":4,"k":"will","ts","n","iv","ct","sig"}
    //   signed  r4.{ts}.{clientId}.will.{n}.{iv}.{ct}
    //
    // The v3 report seal with a third binding: the MQTT client id, exactly as
    // sent to the broker, because that is the topic a will is published on.
    // `ct` holds lastWillReport(n, firmwareProvenanceId). `iv` and `nonce`
    // must be fresh random bytes for every connection attempt, and
    // `timestamp` the time the will is built: the dashboard does not compare
    // it with the time the will arrives, which can be days later.
    bool sealLastWill(const char* clientId, const char* firmwareProvenanceId,
                      std::uint64_t timestamp,
                      const std::uint8_t (&iv)[COMMAND_IV_BYTES],
                      const std::uint8_t (&nonce)[REPORT_NONCE_BYTES],
                      std::string& envelope) const {
        envelope.clear();
        if (!hasKey_ || !clientId || !*clientId ||
            timestamp < LAST_WILL_EARLIEST_MS ||
            timestamp > COMMAND_MAX_TIMESTAMP) return false;
        const std::string nonceText = base64UrlEncode(nonce, sizeof(nonce));
        const std::string report = lastWillReport(nonceText, firmwareProvenanceId);
        const std::size_t padding = COMMAND_AES_BLOCK - report.size() % COMMAND_AES_BLOCK;
        std::vector<std::uint8_t> plain(report.begin(), report.end());
        plain.insert(plain.end(), padding, static_cast<std::uint8_t>(padding));
        std::vector<std::uint8_t> cipher(plain.size());
        if (!aes256CbcEncrypt(reportEncKey_, iv, plain.data(), plain.size(),
                              cipher.data())) return false;
        const std::string ivText = base64UrlEncode(iv, sizeof(iv));
        const std::string cipherText = base64UrlEncode(cipher.data(), cipher.size());
        const std::string message = "r4." + std::to_string(timestamp) + "." +
                                    clientId + ".will." + nonceText + "." +
                                    ivText + "." + cipherText;
        std::uint8_t signature[HMAC_SHA256_BYTES];
        if (!hmacSha256(reportMacKey_, sizeof(reportMacKey_),
                        reinterpret_cast<const std::uint8_t*>(message.data()),
                        message.size(), signature)) return false;
        JsonDocument document;
        document["v"] = 4;
        document["k"] = "will";
        document["ts"] = timestamp;
        document["n"] = nonceText;
        document["iv"] = ivText;
        document["ct"] = cipherText;
        document["sig"] = base64UrlEncode(signature, sizeof(signature));
        serializeJson(document, envelope);
        return true;
    }

 private:
    std::uint8_t macKey_[HMAC_SHA256_BYTES] = {};
    std::uint8_t encKey_[HMAC_SHA256_BYTES] = {};
    std::uint8_t reportMacKey_[HMAC_SHA256_BYTES] = {};
    std::uint8_t reportEncKey_[HMAC_SHA256_BYTES] = {};
    bool hasKey_ = false;
    bool signaturesRequired_ = false;
    std::uint64_t lastTimestamp_ = 0;
    std::array<std::array<std::uint8_t, COMMAND_REPLAY_PREFIX>, COMMAND_REPLAY_RING> ring_{};
    std::size_t ringCount_ = 0;
    std::size_t ringNext_ = 0;
};

}  // namespace lockbox::security
