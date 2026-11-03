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

enum class CommandGateResult {
    plain,          // No key is held: the frame is today's plain JSON.
    opened,         // A verified and decrypted v2 envelope.
    malformed,
    badSignature,
    stale,
    replayed,
    decryptFailed,
};

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

// Holding a key means every command must be a valid v2 envelope. Holding none
// leaves unenrolled behaviour unchanged. The server only hands a key to a device
// it signs for, and a TLS-authenticated auth response can withdraw it.
class CommandGate {
 public:
    bool hasKey() const { return hasKey_; }
    std::uint64_t lastTimestamp() const { return lastTimestamp_; }
    void restoreLastTimestamp(std::uint64_t timestamp) {
        lastTimestamp_ = timestamp;
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
        hasKey_ = true;
        ringCount_ = 0;
        ringNext_ = 0;
        return true;
    }

    void clearKey() {
        std::memset(macKey_, 0, sizeof(macKey_));
        std::memset(encKey_, 0, sizeof(encKey_));
        std::memset(reportMacKey_, 0, sizeof(reportMacKey_));
        std::memset(reportEncKey_, 0, sizeof(reportEncKey_));
        hasKey_ = false;
        ringCount_ = 0;
        ringNext_ = 0;
    }

    CommandGateResult open(const char* mac, const char* frame, std::size_t size,
                           std::string& command) {
        command.clear();
        if (!hasKey_) {
            command.assign(frame, size);
            return CommandGateResult::plain;
        }
        JsonDocument document;
        if (!mac || deserializeJson(document, frame, size)) {
            return CommandGateResult::malformed;
        }
        const JsonObjectConst envelope = document.as<JsonObjectConst>();
        if (envelope.isNull() || envelope.size() != 5 ||
            !envelope["v"].is<int>() || envelope["v"].as<int>() != 2 ||
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
        std::string signedText = "v2.";
        signedText += std::to_string(timestamp);
        signedText += '.';
        for (const char* cursor = mac; *cursor; ++cursor) {
            signedText += static_cast<char>(
                std::toupper(static_cast<unsigned char>(*cursor)));
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

        if (timestamp + COMMAND_REORDER_WINDOW_MS < lastTimestamp_) {
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
    // keys and an "r2." signature prefix mean a captured report can never be
    // opened, or replayed, as a command. `iv` must be fresh random bytes.
    bool seal(const char* mac, const std::string& report, std::uint64_t timestamp,
              const std::uint8_t (&iv)[COMMAND_IV_BYTES], std::string& envelope) const {
        envelope.clear();
        if (!hasKey_ || !mac || report.empty() || report.size() > COMMAND_MAX_TEXT ||
            timestamp == 0 || timestamp > COMMAND_MAX_TIMESTAMP) return false;
        const std::size_t padding = COMMAND_AES_BLOCK - report.size() % COMMAND_AES_BLOCK;
        std::vector<std::uint8_t> plain(report.begin(), report.end());
        plain.insert(plain.end(), padding, static_cast<std::uint8_t>(padding));
        std::vector<std::uint8_t> cipher(plain.size());
        if (!aes256CbcEncrypt(reportEncKey_, iv, plain.data(), plain.size(),
                              cipher.data())) return false;
        const std::string ivText = base64UrlEncode(iv, sizeof(iv));
        const std::string cipherText = base64UrlEncode(cipher.data(), cipher.size());
        const std::string message = "r2." + std::to_string(timestamp) + "." + mac +
                                    "." + ivText + "." + cipherText;
        std::uint8_t signature[HMAC_SHA256_BYTES];
        if (!hmacSha256(reportMacKey_, sizeof(reportMacKey_),
                        reinterpret_cast<const std::uint8_t*>(message.data()),
                        message.size(), signature)) return false;
        JsonDocument document;
        document["v"] = 2;
        document["ts"] = timestamp;
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
    std::uint64_t lastTimestamp_ = 0;
    std::array<std::array<std::uint8_t, COMMAND_REPLAY_PREFIX>, COMMAND_REPLAY_RING> ring_{};
    std::size_t ringCount_ = 0;
    std::size_t ringNext_ = 0;
};

}  // namespace lockbox::security
