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
    // A cleared gate that does not require signatures is plain again. The
    // firmware never clears a key it was given (SigningPolicy.h).
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

// Reports from the box use the same command key with their own labels. Shared
// with rad-app lib/lockbox-device/__tests__/reports.spec.ts.
const char* REPORT_PLAINTEXT = R"({"id":"r1","state":"locked"})";
const char* REPORT_CT = "cgockepMrxiE4IyVbiid_TrENQtDDxNEVUzy8OLa9NM";
const char* REPORT_SIG = "EBC0TL_w-NkbHSlhesWvuTcyCrTjW2tuqD8eYsDvDRM";

void aes_encrypt_matches_fips_197() {
    std::uint8_t key[32];
    for (std::size_t index = 0; index < sizeof(key); ++index) {
        key[index] = static_cast<std::uint8_t>(index);
    }
    const std::uint8_t plain[16] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
                                    0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    const std::uint8_t zeroIv[16] = {};
    std::uint8_t cipher[16];
    std::uint8_t back[16];
    TEST_ASSERT_TRUE(aes256CbcEncrypt(key, zeroIv, plain, sizeof(plain), cipher));
    TEST_ASSERT_EQUAL_STRING("8ea2b7ca516745bfeafc49904b496089",
                             hex(cipher, sizeof(cipher)).c_str());
    TEST_ASSERT_TRUE(aes256CbcDecrypt(key, zeroIv, cipher, sizeof(cipher), back));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(plain, back, sizeof(plain));
}

void reports_seal_to_the_shared_vector() {
    CommandGate gate;
    TEST_ASSERT_TRUE(gate.setKey(COMMAND_KEY, std::strlen(COMMAND_KEY)));
    std::uint8_t iv[COMMAND_IV_BYTES];
    for (std::size_t index = 0; index < sizeof(iv); ++index) {
        iv[index] = static_cast<std::uint8_t>(index);
    }
    std::string envelope;
    TEST_ASSERT_TRUE(gate.seal(MAC, REPORT_PLAINTEXT, TS, iv, envelope));
    JsonDocument document;
    TEST_ASSERT_FALSE(deserializeJson(document, envelope));
    TEST_ASSERT_EQUAL_UINT32(5, document.as<JsonObjectConst>().size());
    TEST_ASSERT_EQUAL_INT(2, document["v"].as<int>());
    TEST_ASSERT_TRUE(document["ts"].as<std::uint64_t>() == TS);
    TEST_ASSERT_EQUAL_STRING(IV, document["iv"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(REPORT_CT, document["ct"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(REPORT_SIG, document["sig"].as<const char*>());
}

void a_sealed_report_never_opens_as_a_command() {
    CommandGate gate;
    TEST_ASSERT_TRUE(gate.setKey(COMMAND_KEY, std::strlen(COMMAND_KEY)));
    std::uint8_t iv[COMMAND_IV_BYTES] = {};
    std::string envelope;
    TEST_ASSERT_TRUE(gate.seal(MAC, R"({"type":"sync","clear":true})", TS, iv, envelope));
    std::string command;
    TEST_ASSERT_TRUE(gate.open(MAC, envelope.data(), envelope.size(), command) ==
                     CommandGateResult::badSignature);
    TEST_ASSERT_TRUE(command.empty());
}

// v2 sign-ins bind to the eFuse device id instead of the MAC. Shared with
// rad-app lib/lockbox-device/__tests__ (deviceIdVector / reportVector).
const char* DEVICE_ID = "78727da905678f5eff040a5e1f42df7f";
const char* V3_COMMAND = R"({"type":"reboot"})";
const char* V3_CT = "FQraH0pn2pB0WUFN8NHim0WWQW5Z_ek5PPFDPnYQHTA";
const char* V3_SIG = "G12OHImd5Ls5f4qzCSpyZS0HgYoQ_8fqwwtB9wHx0Ek";
const char* V3_REPORT_NONCE = "8PHy8_T19vf4-fr7_P3-_w";
const char* V3_REPORT_SIG = "6d8k-qDjeEel3TB7-xDhwkGYF1D-BS-6kgluFSCMBBk";

std::string v3Envelope(std::uint64_t ts, const std::string& ct, const std::string& sig,
                       int version = 3) {
    return "{\"v\":" + std::to_string(version) + ",\"ts\":" + std::to_string(ts) +
           ",\"iv\":\"" + IV + "\",\"ct\":\"" + ct + "\",\"sig\":\"" + sig + "\"}";
}

CommandGateResult openV3(CommandGate& gate, const std::string& frame, std::string& command,
                         std::uint64_t nowMs = 0, const char* deviceId = DEVICE_ID) {
    return gate.open(deviceId, EnvelopeBinding::deviceId, frame.data(), frame.size(),
                     command, nowMs);
}

void v3_vector_opens_only_for_this_device_id() {
    CommandGate gate = keyedGate();
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::opened,
                      openV3(gate, v3Envelope(TS, V3_CT, V3_SIG), command));
    TEST_ASSERT_EQUAL_STRING(V3_COMMAND, command.c_str());

    CommandGate other = keyedGate();
    // The device id is never case-folded, and another id never verifies.
    TEST_ASSERT_EQUAL(CommandGateResult::badSignature,
                      openV3(other, v3Envelope(TS, V3_CT, V3_SIG), command, 0,
                             "78727DA905678F5EFF040A5E1F42DF7F"));
    TEST_ASSERT_EQUAL(CommandGateResult::badSignature,
                      openV3(other, v3Envelope(TS, V3_CT, V3_SIG), command, 0,
                             "00727da905678f5eff040a5e1f42df7f"));
}

void envelope_versions_follow_the_sign_in() {
    // A v2 sign-in takes only v3, a v1 sign-in only v2; the signed prefixes
    // differ too, so relabelling the version does not help.
    CommandGate gate = keyedGate();
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::malformed,
                      openV3(gate, envelope(TS, IV, CT, SIG), command));
    TEST_ASSERT_EQUAL(CommandGateResult::malformed,
                      openText(gate, v3Envelope(TS, V3_CT, V3_SIG), command));
    TEST_ASSERT_EQUAL(CommandGateResult::badSignature,
                      openText(gate, v3Envelope(TS, V3_CT, V3_SIG, 2), command));
    TEST_ASSERT_EQUAL(CommandGateResult::badSignature,
                      openV3(gate, v3Envelope(TS, CT, SIG, 3), command, 0, MAC));
}

void a_burned_box_refuses_plain_json_without_a_key() {
    CommandGate gate;
    gate.requireSignatures(true);
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::unsignedRefused,
                      gate.open(MAC, PLAINTEXT, std::strlen(PLAINTEXT), command));
    TEST_ASSERT_TRUE(command.empty());
    TEST_ASSERT_FALSE(commandGateAccepted(CommandGateResult::unsignedRefused));
    TEST_ASSERT_TRUE(gate.setKey(COMMAND_KEY, std::strlen(COMMAND_KEY)));
    TEST_ASSERT_EQUAL(CommandGateResult::opened,
                      openV3(gate, v3Envelope(TS, V3_CT, V3_SIG), command));
    gate.clearKey();
    TEST_ASSERT_EQUAL(CommandGateResult::unsignedRefused,
                      gate.open(MAC, PLAINTEXT, std::strlen(PLAINTEXT), command));
}

void a_new_sign_in_keeps_spent_commands_spent() {
    CommandGate gate = keyedGate();
    std::string command;
    TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(gate, signedAt(TS), command));
    // Every sign-in hands the key over again; the ring must survive it.
    TEST_ASSERT_TRUE(gate.setKey(COMMAND_KEY, std::strlen(COMMAND_KEY)));
    TEST_ASSERT_EQUAL(CommandGateResult::replayed, openText(gate, signedAt(TS), command));
    gate.clearKey();
    TEST_ASSERT_TRUE(gate.setKey(COMMAND_KEY, std::strlen(COMMAND_KEY)));
    TEST_ASSERT_EQUAL(CommandGateResult::replayed, openText(gate, signedAt(TS), command));
}

void replay_state_survives_a_reboot() {
    CommandGate gate = keyedGate();
    std::string command;
    for (std::uint64_t step = 0; step < 3; ++step) {
        TEST_ASSERT_EQUAL(CommandGateResult::opened,
                          openText(gate, signedAt(TS + step), command));
    }
    std::uint8_t state[COMMAND_REPLAY_STATE_BYTES];
    gate.exportReplayState(state);

    // A fresh gate, as after a reboot, restored from storage.
    CommandGate rebooted = keyedGate();
    TEST_ASSERT_TRUE(rebooted.restoreReplayState(state, sizeof(state)));
    TEST_ASSERT_EQUAL_UINT64(TS + 2, rebooted.lastTimestamp());
    for (std::uint64_t step = 0; step < 3; ++step) {
        TEST_ASSERT_EQUAL(CommandGateResult::replayed,
                          openText(rebooted, signedAt(TS + step), command));
    }
    TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(rebooted, signedAt(TS + 3), command));

    // A corrupt record is refused and leaves the gate as it was.
    CommandGate untouched = keyedGate();
    std::uint8_t bad[COMMAND_REPLAY_STATE_BYTES];
    std::memcpy(bad, state, sizeof(bad));
    bad[8] = COMMAND_REPLAY_RING + 1;
    TEST_ASSERT_FALSE(untouched.restoreReplayState(bad, sizeof(bad)));
    std::memcpy(bad, state, sizeof(bad));
    bad[9] = COMMAND_REPLAY_RING;
    TEST_ASSERT_FALSE(untouched.restoreReplayState(bad, sizeof(bad)));
    TEST_ASSERT_FALSE(untouched.restoreReplayState(state, sizeof(state) - 1));
    TEST_ASSERT_FALSE(untouched.restoreReplayState(nullptr, sizeof(state)));
    TEST_ASSERT_EQUAL_UINT64(0, untouched.lastTimestamp());
    TEST_ASSERT_EQUAL(CommandGateResult::opened, openText(untouched, signedAt(TS), command));
}

void a_verified_clock_refuses_old_recordings() {
    CommandGate gate = keyedGate();
    std::string command;
    const std::uint64_t now = TS + COMMAND_MAX_AGE_MS + 1;
    TEST_ASSERT_EQUAL(CommandGateResult::stale,
                      openV3(gate, v3Envelope(TS, V3_CT, V3_SIG), command, now));
    TEST_ASSERT_TRUE(command.empty());
    TEST_ASSERT_EQUAL_UINT64(0, gate.lastTimestamp());
    // Within the window, or with no verified clock, it opens.
    TEST_ASSERT_EQUAL(CommandGateResult::opened,
                      openV3(gate, v3Envelope(TS, V3_CT, V3_SIG), command,
                             TS + COMMAND_MAX_AGE_MS));
    CommandGate unverified = keyedGate();
    TEST_ASSERT_EQUAL(CommandGateResult::opened,
                      openV3(unverified, v3Envelope(TS, V3_CT, V3_SIG), command, 0));
}

void v3_reports_seal_to_the_shared_vector() {
    CommandGate gate = keyedGate();
    std::uint8_t iv[COMMAND_IV_BYTES], nonce[REPORT_NONCE_BYTES];
    for (std::size_t index = 0; index < sizeof(iv); ++index) {
        iv[index] = static_cast<std::uint8_t>(index);
        nonce[index] = static_cast<std::uint8_t>(0xf0 + index);
    }
    std::string sealed;
    TEST_ASSERT_TRUE(gate.seal(DEVICE_ID, EnvelopeBinding::deviceId, ReportKind::state,
                               REPORT_PLAINTEXT, TS, iv, nonce, sealed));
    JsonDocument document;
    TEST_ASSERT_FALSE(deserializeJson(document, sealed));
    TEST_ASSERT_EQUAL_UINT32(7, document.as<JsonObjectConst>().size());
    TEST_ASSERT_EQUAL_INT(3, document["v"].as<int>());
    TEST_ASSERT_TRUE(document["ts"].as<std::uint64_t>() == TS);
    TEST_ASSERT_EQUAL_STRING("state", document["k"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(V3_REPORT_NONCE, document["n"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(IV, document["iv"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(REPORT_CT, document["ct"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(V3_REPORT_SIG, document["sig"].as<const char*>());

    // The kind is signed: a log seals to a different signature.
    std::string log;
    TEST_ASSERT_TRUE(gate.seal(DEVICE_ID, EnvelopeBinding::deviceId, ReportKind::log,
                               REPORT_PLAINTEXT, TS, iv, nonce, log));
    JsonDocument logDocument;
    TEST_ASSERT_FALSE(deserializeJson(logDocument, log));
    TEST_ASSERT_EQUAL_STRING("log", logDocument["k"].as<const char*>());
    TEST_ASSERT_TRUE(std::strcmp(V3_REPORT_SIG, logDocument["sig"].as<const char*>()) != 0);
}

// Shared with rad-app's lib/lockbox-device/__tests__/lastWill.spec.ts. The
// will is bound to the MQTT client id, the topic the broker publishes it on.
const char* WILL_CLIENT_ID = "05e461ba-c17c-4585-888a-d95c3ad3f701";
const char* WILL_PLAINTEXT =
    R"({"id":"8PHy8_T19vf4-fr7_P3-_w","state":"disconnected","createdAt":0,"firmwareProvenanceId":""})";
const char* WILL_CT =
    "_rXf39t6ewGJSsm4BAvpt4kodu_1bQtIwEVrY3pN-hcW067DAlNa32ZWEEGalj2BZcuPsw8JXxXGr7e7"
    "tfXUeSi2O9WGqyYIs_FwphQfX-xrPUvWZC5OnVUoFUwRT4W3";
const char* WILL_SIG = "9tfxsWlJibkZex4XCC2vgHbLwaThMsmROMFJ_qMhO2E";

void last_will_seals_to_the_shared_vector() {
    CommandGate gate = keyedGate();
    std::uint8_t iv[COMMAND_IV_BYTES], nonce[REPORT_NONCE_BYTES];
    for (std::size_t index = 0; index < sizeof(iv); ++index) {
        iv[index] = static_cast<std::uint8_t>(index);
        nonce[index] = static_cast<std::uint8_t>(0xf0 + index);
    }
    // The report inside names the nonce as its id and carries no clock.
    TEST_ASSERT_EQUAL_STRING(WILL_PLAINTEXT,
                             lastWillReport(V3_REPORT_NONCE, "").c_str());
    TEST_ASSERT_EQUAL_STRING(WILL_PLAINTEXT,
                             lastWillReport(V3_REPORT_NONCE, nullptr).c_str());

    std::string sealed;
    TEST_ASSERT_TRUE(gate.sealLastWill(WILL_CLIENT_ID, "", TS, iv, nonce, sealed));
    JsonDocument document;
    TEST_ASSERT_FALSE(deserializeJson(document, sealed));
    // Exactly the seven fields the dashboard accepts.
    TEST_ASSERT_EQUAL_UINT32(7, document.as<JsonObjectConst>().size());
    TEST_ASSERT_TRUE(document["v"].is<int>());
    TEST_ASSERT_EQUAL_INT(4, document["v"].as<int>());
    TEST_ASSERT_EQUAL_STRING("will", document["k"].as<const char*>());
    TEST_ASSERT_TRUE(document["ts"].is<std::uint64_t>());
    TEST_ASSERT_TRUE(document["ts"].as<std::uint64_t>() == TS);
    TEST_ASSERT_EQUAL_STRING(V3_REPORT_NONCE, document["n"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(IV, document["iv"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(WILL_CT, document["ct"].as<const char*>());
    TEST_ASSERT_EQUAL_STRING(WILL_SIG, document["sig"].as<const char*>());
}

void last_will_is_bound_to_the_client_id_and_new_every_time() {
    CommandGate gate = keyedGate();
    std::uint8_t iv[COMMAND_IV_BYTES], nonce[REPORT_NONCE_BYTES];
    for (std::size_t index = 0; index < sizeof(iv); ++index) {
        iv[index] = static_cast<std::uint8_t>(index);
        nonce[index] = static_cast<std::uint8_t>(0xf0 + index);
    }
    const auto field = [](const std::string& sealed, const char* key) {
        JsonDocument document;
        TEST_ASSERT_FALSE(deserializeJson(document, sealed));
        return std::string(document[key].as<const char*>());
    };
    std::string sealed;

    // Another client id signs differently: a will cannot be moved to another
    // box's topic.
    TEST_ASSERT_TRUE(gate.sealLastWill("15e461ba-c17c-4585-888a-d95c3ad3f701", "",
                                       TS, iv, nonce, sealed));
    TEST_ASSERT_EQUAL_STRING(WILL_CT, field(sealed, "ct").c_str());
    TEST_ASSERT_TRUE(field(sealed, "sig") != WILL_SIG);

    // A later connection attempt: its time is signed.
    TEST_ASSERT_TRUE(gate.sealLastWill(WILL_CLIENT_ID, "", TS + 1, iv, nonce, sealed));
    TEST_ASSERT_TRUE(field(sealed, "sig") != WILL_SIG);

    // A fresh nonce changes the id inside, and with it the ciphertext.
    nonce[0] ^= 0x01;
    TEST_ASSERT_TRUE(gate.sealLastWill(WILL_CLIENT_ID, "", TS, iv, nonce, sealed));
    TEST_ASSERT_TRUE(field(sealed, "n") != V3_REPORT_NONCE);
    TEST_ASSERT_TRUE(field(sealed, "ct") != WILL_CT);
    TEST_ASSERT_TRUE(field(sealed, "sig") != WILL_SIG);
    nonce[0] ^= 0x01;

    // A fresh IV changes the ciphertext too.
    iv[0] ^= 0x01;
    TEST_ASSERT_TRUE(gate.sealLastWill(WILL_CLIENT_ID, "", TS, iv, nonce, sealed));
    TEST_ASSERT_TRUE(field(sealed, "ct") != WILL_CT);
}

void a_sealed_last_will_never_opens_as_a_command_or_a_report() {
    CommandGate gate = keyedGate();
    std::uint8_t iv[COMMAND_IV_BYTES] = {}, nonce[REPORT_NONCE_BYTES] = {};
    std::string sealed, command;
    TEST_ASSERT_TRUE(gate.sealLastWill(WILL_CLIENT_ID, "", TS, iv, nonce, sealed));
    // Seven fields and version 4: not a command envelope for any binding.
    TEST_ASSERT_EQUAL(CommandGateResult::malformed,
                      gate.open(WILL_CLIENT_ID, EnvelopeBinding::deviceId,
                                sealed.data(), sealed.size(), command));
    TEST_ASSERT_EQUAL(CommandGateResult::malformed,
                      gate.open(MAC, sealed.data(), sealed.size(), command));
    TEST_ASSERT_TRUE(command.empty());

    // The same nonce, IV and time sealed as a v3 state report for the same
    // name signs differently: neither can stand in for the other.
    std::string report;
    TEST_ASSERT_TRUE(gate.seal(WILL_CLIENT_ID, EnvelopeBinding::deviceId,
                               ReportKind::state, lastWillReport(
                                   base64UrlEncode(nonce, sizeof(nonce)), ""),
                               TS, iv, nonce, report));
    JsonDocument will, state;
    TEST_ASSERT_FALSE(deserializeJson(will, sealed));
    TEST_ASSERT_FALSE(deserializeJson(state, report));
    TEST_ASSERT_EQUAL_STRING(will["ct"].as<const char*>(),
                             state["ct"].as<const char*>());
    TEST_ASSERT_TRUE(std::strcmp(will["sig"].as<const char*>(),
                                 state["sig"].as<const char*>()) != 0);
}

void sealing_a_last_will_needs_a_key_a_client_id_and_a_plausible_clock() {
    std::uint8_t iv[COMMAND_IV_BYTES] = {}, nonce[REPORT_NONCE_BYTES] = {};
    std::string sealed = "stale";
    CommandGate keyless;
    TEST_ASSERT_FALSE(keyless.sealLastWill(WILL_CLIENT_ID, "", TS, iv, nonce, sealed));
    TEST_ASSERT_TRUE(sealed.empty());

    CommandGate gate = keyedGate();
    TEST_ASSERT_FALSE(gate.sealLastWill("", "", TS, iv, nonce, sealed));
    TEST_ASSERT_FALSE(gate.sealLastWill(nullptr, "", TS, iv, nonce, sealed));
    // A clock that was never set would only earn a refusal.
    TEST_ASSERT_FALSE(gate.sealLastWill(WILL_CLIENT_ID, "", 0, iv, nonce, sealed));
    TEST_ASSERT_FALSE(gate.sealLastWill(WILL_CLIENT_ID, "", LAST_WILL_EARLIEST_MS - 1,
                                        iv, nonce, sealed));
    TEST_ASSERT_TRUE(sealed.empty());
    TEST_ASSERT_TRUE(gate.sealLastWill(WILL_CLIENT_ID, "", LAST_WILL_EARLIEST_MS, iv,
                                       nonce, sealed));
    gate.clearKey();
    TEST_ASSERT_FALSE(gate.sealLastWill(WILL_CLIENT_ID, "", TS, iv, nonce, sealed));
}

void sealing_needs_a_key_and_a_clock() {
    CommandGate gate;
    std::uint8_t iv[COMMAND_IV_BYTES] = {};
    std::string envelope;
    TEST_ASSERT_FALSE(gate.seal(MAC, REPORT_PLAINTEXT, TS, iv, envelope));
    TEST_ASSERT_TRUE(gate.setKey(COMMAND_KEY, std::strlen(COMMAND_KEY)));
    TEST_ASSERT_FALSE(gate.seal(MAC, REPORT_PLAINTEXT, 0, iv, envelope));
    gate.clearKey();
    TEST_ASSERT_FALSE(gate.seal(MAC, REPORT_PLAINTEXT, TS, iv, envelope));
    TEST_ASSERT_TRUE(envelope.empty());
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
    RUN_TEST(aes_encrypt_matches_fips_197);
    RUN_TEST(reports_seal_to_the_shared_vector);
    RUN_TEST(a_sealed_report_never_opens_as_a_command);
    RUN_TEST(sealing_needs_a_key_and_a_clock);
    RUN_TEST(v3_vector_opens_only_for_this_device_id);
    RUN_TEST(envelope_versions_follow_the_sign_in);
    RUN_TEST(a_burned_box_refuses_plain_json_without_a_key);
    RUN_TEST(a_new_sign_in_keeps_spent_commands_spent);
    RUN_TEST(replay_state_survives_a_reboot);
    RUN_TEST(a_verified_clock_refuses_old_recordings);
    RUN_TEST(v3_reports_seal_to_the_shared_vector);
    RUN_TEST(last_will_seals_to_the_shared_vector);
    RUN_TEST(last_will_is_bound_to_the_client_id_and_new_every_time);
    RUN_TEST(a_sealed_last_will_never_opens_as_a_command_or_a_report);
    RUN_TEST(sealing_a_last_will_needs_a_key_a_client_id_and_a_plausible_clock);
    return UNITY_END();
}
