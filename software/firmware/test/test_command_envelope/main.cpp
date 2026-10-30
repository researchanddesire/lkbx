#include <ArduinoJson.h>
#include <CommandEnvelope.h>
#include <MqttFrame.h>
#include <unity.h>

#include <cstdio>
#include <string>

using namespace lockbox::security;

namespace {
// Shared with the server's test suite. Not a live secret.
const char* MASTER = "test-master-key-not-a-secret";
const char* MAC = "AA:BB:CC:DD:EE:FF";
const char* COMMAND_KEY = "BWt_KAZ_bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOA";  // gitleaks:allow
const char* MAC_KEY_HEX =
    "b23190b291d65658ed173c8e0b3b3e671cc3447d577dfc49143a2794f9fd3010";
const char* ENC_KEY_HEX =
    "e4877ab3f2cfa2fcbdff2492da3f7874815fc26c393324a2ce8a96046c83f7a0";
const char* PLAINTEXT = R"({"type":"sync","clear":true})";
const std::uint64_t TS = 1788800000000ULL;
const char* IV = "AAECAwQFBgcICQoLDA0ODw";
const char* CT = "3-KeLqCpZZdL_p0ISyHDGrbDXx1hvZzT1FjyiUclTBU";
const char* SIG = "omqryKgNjSUIBgfZn-PHMKSvFnv2b0rpHRkZ_rpXe-A";

std::string hex(const std::uint8_t* data, std::size_t length) {
    std::string text;
    char part[3];
    for (std::size_t index = 0; index < length; ++index) {
        std::snprintf(part, sizeof(part), "%02x", data[index]);
        text += part;
    }
    return text;
}

std::string hmacText(const std::uint8_t* key, std::size_t keyLength,
                     const std::string& message) {
    std::uint8_t digest[HMAC_SHA256_BYTES];
    TEST_ASSERT_TRUE(hmacSha256(key, keyLength,
                                reinterpret_cast<const std::uint8_t*>(message.data()),
                                message.size(), digest));
    return std::string(reinterpret_cast<const char*>(digest), sizeof(digest));
}

std::string macKey() {
    std::uint8_t key[COMMAND_KEY_BYTES];
    std::size_t written = 0;
    TEST_ASSERT_TRUE(base64UrlDecode(COMMAND_KEY, std::strlen(COMMAND_KEY), key,
                                     sizeof(key), written));
    return hmacText(key, sizeof(key), "lkbx-cmd-mac-v2");
}

std::string signature(std::uint64_t ts, const std::string& mac, const std::string& iv,
                      const std::string& ct) {
    const std::string key = macKey();
    const std::string digest =
        hmacText(reinterpret_cast<const std::uint8_t*>(key.data()), key.size(),
                 "v2." + std::to_string(ts) + "." + mac + "." + iv + "." + ct);
    return base64UrlEncode(reinterpret_cast<const std::uint8_t*>(digest.data()),
                           digest.size());
}

std::string envelope(std::uint64_t ts, const std::string& iv, const std::string& ct,
                     const std::string& sig) {
    return "{\"v\":2,\"ts\":" + std::to_string(ts) + ",\"iv\":\"" + iv + "\",\"ct\":\"" +
           ct + "\",\"sig\":\"" + sig + "\"}";
}

// The same ciphertext signed at another time: an authentic, different envelope.
std::string signedAt(std::uint64_t ts) {
    return envelope(ts, IV, CT, signature(ts, MAC, IV, CT));
}

CommandGate keyedGate() {
    CommandGate gate;
    TEST_ASSERT_TRUE(gate.setKey(COMMAND_KEY, std::strlen(COMMAND_KEY)));
    return gate;
}

CommandGateResult openText(CommandGate& gate, const std::string& frame,
                           std::string& command, const char* mac = MAC) {
    return gate.open(mac, frame.data(), frame.size(), command);
}
}  // namespace

void setUp() {}
void tearDown() {}

void primitives_match_published_vectors() {
    // RFC 4231 test case 2.
    const std::string jefe = "Jefe";
    const std::string digest =
        hmacText(reinterpret_cast<const std::uint8_t*>(jefe.data()), jefe.size(),
                 "what do ya want for nothing?");
    TEST_ASSERT_EQUAL_STRING(
        "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
        hex(reinterpret_cast<const std::uint8_t*>(digest.data()), digest.size()).c_str());

    // FIPS-197 appendix C.3, as one CBC block with a zero IV.
    std::uint8_t key[32], cipher[16], plain[16];
    const std::uint8_t iv[16] = {};
    for (int index = 0; index < 32; ++index) key[index] = static_cast<std::uint8_t>(index);
    const std::uint8_t cipherBytes[16] = {0x8e, 0xa2, 0xb7, 0xca, 0x51, 0x67, 0x45, 0xbf,
                                          0xea, 0xfc, 0x49, 0x90, 0x4b, 0x49, 0x60, 0x89};
    std::memcpy(cipher, cipherBytes, sizeof(cipher));
    TEST_ASSERT_TRUE(aes256CbcDecrypt(key, iv, cipher, sizeof(cipher), plain));
    TEST_ASSERT_EQUAL_STRING("00112233445566778899aabbccddeeff",
                             hex(plain, sizeof(plain)).c_str());
}

void shared_vector_derives_the_same_keys() {
    const std::string command =
        hmacText(reinterpret_cast<const std::uint8_t*>(MASTER), std::strlen(MASTER),
                 std::string("lkbx-cmd-v1:") + MAC + ":1");
    TEST_ASSERT_EQUAL_STRING(
        COMMAND_KEY,
        base64UrlEncode(reinterpret_cast<const std::uint8_t*>(command.data()),
                        command.size()).c_str());
    std::uint8_t key[COMMAND_KEY_BYTES];
    std::size_t written = 0;
    TEST_ASSERT_TRUE(base64UrlDecode(COMMAND_KEY, std::strlen(COMMAND_KEY), key,
                                     sizeof(key), written));
    TEST_ASSERT_EQUAL(COMMAND_KEY_BYTES, written);
    const std::string mac = hmacText(key, sizeof(key), "lkbx-cmd-mac-v2");
    const std::string enc = hmacText(key, sizeof(key), "lkbx-cmd-enc-v2");
    TEST_ASSERT_EQUAL_STRING(
        MAC_KEY_HEX,
        hex(reinterpret_cast<const std::uint8_t*>(mac.data()), mac.size()).c_str());
    TEST_ASSERT_EQUAL_STRING(
        ENC_KEY_HEX,
        hex(reinterpret_cast<const std::uint8_t*>(enc.data()), enc.size()).c_str());
}

void shared_vector_opens_to_the_exact_command() {
    CommandGate gate = keyedGate();
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::opened,
                      openText(gate, envelope(TS, IV, CT, SIG), command));
    TEST_ASSERT_EQUAL_STRING(PLAINTEXT, command.c_str());
    TEST_ASSERT_EQUAL_UINT64(TS, gate.lastTimestamp());
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, command));
    TEST_ASSERT_TRUE(commandResponse(doc.as<JsonVariantConst>()));
}

void mac_is_compared_in_uppercase() {
    CommandGate gate = keyedGate();
    std::string command;
    TEST_ASSERT_EQUAL(
        CommandGateResult::opened,
        openText(gate, envelope(TS, IV, CT, SIG), command, "aa:bb:cc:dd:ee:ff"));
}

void any_tampering_fails_the_signature() {
    const std::string otherCt = "4-KeLqCpZZdL_p0ISyHDGrbDXx1hvZzT1FjyiUclTBU";
    const std::string otherIv = "AQECAwQFBgcICQoLDA0ODw";
    const std::string otherSig = "pmqryKgNjSUIBgfZn-PHMKSvFnv2b0rpHRkZ_rpXe-A";
    const std::string frames[] = {envelope(TS, IV, otherCt, SIG),
                                  envelope(TS, otherIv, CT, SIG),
                                  envelope(TS, IV, CT, otherSig),
                                  envelope(TS + 1, IV, CT, SIG)};
    for (const auto& frame : frames) {
        CommandGate gate = keyedGate();
        std::string command;
        TEST_ASSERT_EQUAL(CommandGateResult::badSignature, openText(gate, frame, command));
        TEST_ASSERT_TRUE(command.empty());
        TEST_ASSERT_EQUAL_UINT64(0, gate.lastTimestamp());
    }
    CommandGate other = keyedGate();
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::badSignature,
                      openText(other, envelope(TS, IV, CT, SIG), command,
                               "AA:BB:CC:DD:EE:00"));
}

void signature_is_checked_before_decryption() {
    // Authentic ciphertext signed by nobody is refused without being opened;
    // only a correct signature can reach the padding check below.
    const std::string garbage = "AAAAAAAAAAAAAAAAAAAAAA";
    CommandGate gate = keyedGate();
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::badSignature,
                      openText(gate, envelope(TS, IV, garbage, SIG), command));
    TEST_ASSERT_EQUAL(
        CommandGateResult::decryptFailed,
        openText(gate, envelope(TS, IV, garbage, signature(TS, MAC, IV, garbage)), command));
    TEST_ASSERT_EQUAL_UINT64(0, gate.lastTimestamp());
    // A failed decryption does not consume its signature or the timestamp.
    TEST_ASSERT_EQUAL(CommandGateResult::opened,
                      openText(gate, envelope(TS, IV, CT, SIG), command));
}

void holding_a_key_rejects_plain_json() {
    CommandGate gate = keyedGate();
    const char* frames[] = {PLAINTEXT, R"({"type":"reboot"})", "{}", "[]", "not json", ""};
    for (const auto* frame : frames) {
        std::string command;
        TEST_ASSERT_EQUAL_MESSAGE(CommandGateResult::malformed,
                                  gate.open(MAC, frame, std::strlen(frame), command), frame);
        TEST_ASSERT_TRUE(command.empty());
    }
}

void no_key_passes_plain_json_through_unchanged() {
    CommandGate gate;
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::plain,
                      gate.open(MAC, PLAINTEXT, std::strlen(PLAINTEXT), command));
    TEST_ASSERT_EQUAL_STRING(PLAINTEXT, command.c_str());
    TEST_ASSERT_TRUE(commandGateAccepted(CommandGateResult::plain));

    TEST_ASSERT_TRUE(gate.setKey(COMMAND_KEY, std::strlen(COMMAND_KEY)));
    TEST_ASSERT_EQUAL(CommandGateResult::malformed,
                      gate.open(MAC, PLAINTEXT, std::strlen(PLAINTEXT), command));
    // The server can withdraw the key over its TLS-authenticated auth response.
    gate.clearKey();
    TEST_ASSERT_EQUAL(CommandGateResult::plain,
                      gate.open(MAC, PLAINTEXT, std::strlen(PLAINTEXT), command));
}

void malformed_envelopes_are_rejected() {
    const std::string sig = signature(TS, MAC, IV, CT);
    const std::string ts = std::to_string(TS);
    const std::string frames[] = {
        "{\"v\":2,\"ts\":" + ts + ",\"iv\":\"" + IV + "\",\"ct\":\"" + CT + "\"}",
        "{\"v\":2,\"ts\":" + ts + ",\"iv\":\"" + IV + "\",\"ct\":\"" + CT + "\",\"sig\":\"" + sig +
            "\",\"extra\":1}",
        "{\"v\":1,\"ts\":" + ts + ",\"iv\":\"" + IV + "\",\"ct\":\"" + CT + "\",\"sig\":\"" + sig + "\"}",
        "{\"v\":\"2\",\"ts\":" + ts + ",\"iv\":\"" + IV + "\",\"ct\":\"" + CT + "\",\"sig\":\"" + sig + "\"}",
        "{\"v\":2,\"ts\":\"" + ts + "\",\"iv\":\"" + IV + "\",\"ct\":\"" + CT + "\",\"sig\":\"" + sig + "\"}",
        "{\"v\":2,\"ts\":-1,\"iv\":\"" + std::string(IV) + "\",\"ct\":\"" + CT + "\",\"sig\":\"" + sig + "\"}",
        "{\"v\":2,\"ts\":0,\"iv\":\"" + std::string(IV) + "\",\"ct\":\"" + CT + "\",\"sig\":\"" + sig + "\"}",
        "{\"v\":2,\"ts\":" + ts + ".5,\"iv\":\"" + IV + "\",\"ct\":\"" + CT + "\",\"sig\":\"" + sig + "\"}",
        // Short IV, unaligned ciphertext, padded and non-url alphabets, and a
        // non-canonical encoding that decodes to the same bytes.
        envelope(TS, "AAECAwQFBgcICQoLDA0O", CT, sig),
        envelope(TS, IV, "AAECAwQF", sig),
        envelope(TS, IV + std::string("=="), CT, sig),
        envelope(TS, IV, CT, sig + "="),
        envelope(TS, "AAECAwQFBgcICQoLDA0ODx", CT, sig),
        envelope(TS, IV, "3+KeLqCpZZdL/p0ISyHDGrbDXx1hvZzT1FjyiUclTBU", sig),
    };
    for (const auto& frame : frames) {
        CommandGate gate = keyedGate();
        std::string command;
        TEST_ASSERT_EQUAL_MESSAGE(CommandGateResult::malformed, openText(gate, frame, command),
                                  frame.c_str());
    }
}

void exact_replays_are_rejected_within_the_ring() {
    CommandGate gate = keyedGate();
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(gate, signedAt(TS), command));
    TEST_ASSERT_EQUAL(CommandGateResult::replayed, openText(gate, signedAt(TS), command));
    TEST_ASSERT_TRUE(command.empty());
    for (std::uint64_t step = 1; step < COMMAND_REPLAY_RING; ++step) {
        TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(gate, signedAt(TS + step), command));
    }
    TEST_ASSERT_EQUAL(CommandGateResult::replayed, openText(gate, signedAt(TS), command));
    // Eight newer commands push the entry out; the timestamp window still bounds it.
    TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(gate, signedAt(TS + 8), command));
    TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(gate, signedAt(TS), command));
    TEST_ASSERT_EQUAL(CommandGateResult::opened,
                      openText(gate, signedAt(TS + COMMAND_REORDER_WINDOW_MS + 1), command));
    TEST_ASSERT_EQUAL(CommandGateResult::stale, openText(gate, signedAt(TS), command));
}

void reorder_window_accepts_late_and_rejects_stale() {
    CommandGate gate = keyedGate();
    std::string command;
    const std::uint64_t latest = TS + 100000;
    TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(gate, signedAt(latest), command));
    TEST_ASSERT_EQUAL(CommandGateResult::opened,
                      openText(gate, signedAt(latest - COMMAND_REORDER_WINDOW_MS), command));
    TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(gate, signedAt(latest - 1), command));
    // An accepted older command never moves the high-water mark backwards.
    TEST_ASSERT_EQUAL_UINT64(latest, gate.lastTimestamp());
    TEST_ASSERT_EQUAL(CommandGateResult::stale,
                      openText(gate, signedAt(latest - COMMAND_REORDER_WINDOW_MS - 1), command));
    TEST_ASSERT_TRUE(command.empty());
}

void restored_timestamp_survives_a_reboot() {
    CommandGate gate = keyedGate();
    gate.restoreLastTimestamp(TS + COMMAND_REORDER_WINDOW_MS + 1);
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::stale, openText(gate, envelope(TS, IV, CT, SIG), command));
    gate.restoreLastTimestamp(TS + COMMAND_REORDER_WINDOW_MS);
    TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(gate, envelope(TS, IV, CT, SIG), command));
}

void invalid_key_text_is_refused() {
    CommandGate gate;
    for (const auto* text : {"", "short", "BWt_KAZ_bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOA=",
                             "BWt/KAZ+bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOA",
                             "BWt_KAZ_bF4kiAgB54ujyb3xMqmK8mUvEto3EY9FTOB"}) {
        TEST_ASSERT_FALSE_MESSAGE(gate.setKey(text, std::strlen(text)), text);
        TEST_ASSERT_FALSE(gate.hasKey());
    }
    TEST_ASSERT_FALSE(gate.setKey(nullptr, 0));
}

void retained_commands_are_dropped_and_fragments_still_reassemble() {
    MqttFrame<128> frame;
    const char* command = R"({"type":"reboot"})";
    const int length = static_cast<int>(std::strlen(command));
    // Whole and fragmented retained publications never complete.
    TEST_ASSERT_EQUAL(FrameResult::rejected,
                      frame.append("device", "device", 6, command, length, length, 0, true));
    TEST_ASSERT_EQUAL(0, frame.size());
    TEST_ASSERT_EQUAL(FrameResult::rejected,
                      frame.append("device", "device", 6, command, 5, length, 0, true));
    TEST_ASSERT_EQUAL(FrameResult::rejected,
                      frame.append("device", nullptr, 0, command + 5, length - 5, length, 5, true));
    // A later fragment cannot complete a retained first fragment on its own.
    TEST_ASSERT_EQUAL(FrameResult::rejected,
                      frame.append("device", "device", 6, command, 5, length, 0, true));
    TEST_ASSERT_EQUAL(FrameResult::rejected,
                      frame.append("device", nullptr, 0, command + 5, length - 5, length, 5));
    TEST_ASSERT_EQUAL(0, frame.size());
    // Ordinary fragmented publications are unaffected.
    TEST_ASSERT_EQUAL(FrameResult::incomplete,
                      frame.append("device", "device", 6, command, 5, length, 0));
    TEST_ASSERT_EQUAL(FrameResult::complete,
                      frame.append("device", nullptr, 0, command + 5, length - 5, length, 5));
    TEST_ASSERT_EQUAL_STRING(command, frame.data());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(primitives_match_published_vectors);
    RUN_TEST(shared_vector_derives_the_same_keys);
    RUN_TEST(shared_vector_opens_to_the_exact_command);
    RUN_TEST(mac_is_compared_in_uppercase);
    RUN_TEST(any_tampering_fails_the_signature);
    RUN_TEST(signature_is_checked_before_decryption);
    RUN_TEST(holding_a_key_rejects_plain_json);
    RUN_TEST(no_key_passes_plain_json_through_unchanged);
    RUN_TEST(malformed_envelopes_are_rejected);
    RUN_TEST(exact_replays_are_rejected_within_the_ring);
    RUN_TEST(reorder_window_accepts_late_and_rejects_stale);
    RUN_TEST(restored_timestamp_survives_a_reboot);
    RUN_TEST(invalid_key_text_is_refused);
    RUN_TEST(retained_commands_are_dropped_and_fragments_still_reassemble);
    return UNITY_END();
}
