#ifndef LOCKBOX_VOTE_LINK_H
#define LOCKBOX_VOTE_LINK_H

#include <stddef.h>
#include <string.h>

// The page where others vote on the session (today Chaster's Share links page,
// https://chaster.app/sessions/<token>). The lock menu shows it as a QR code so
// people can scan it and add or remove time. The server sends it as `voteUrl`
// in the HTTP state, the MQTT sync and the BLE session definition.
//
// This header is deliberately free of Arduino dependencies so the rules can be
// unit-tested natively. See test/test_vote_link.

// The lock menu draws a version-7 QR code at medium error correction, which
// holds 122 bytes. The stored link, with its NUL, stays inside that.
constexpr size_t kVoteUrlBytes = 121;

// Incoming voting link. `present` distinguishes a missing field from an
// explicit value: older servers, and commands a box receives unencrypted, leave
// the field out, and the box keeps the link it has. An empty `url` clears it.
struct VoteUrlSync {
    bool present = false;
    char url[kVoteUrlBytes] = "";
};

// A link the box will show: https, printable ASCII without spaces, quotes or
// backslashes, and short enough for the QR code.
inline bool voteUrlUsable(const char* text) {
    static const char prefix[] = "https://";
    constexpr size_t prefixLength = sizeof(prefix) - 1;
    if (text == nullptr) return false;
    const size_t length = strlen(text);
    if (length <= prefixLength || length >= kVoteUrlBytes) return false;
    if (strncmp(text, prefix, prefixLength) != 0) return false;
    for (size_t i = 0; i < length; i++) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c <= ' ' || c >= 0x7f || c == '"' || c == '\\') return false;
    }
    return true;
}

// Presence rules shared by the HTTP state, MQTT sync and BLE session parsers:
//
//   * key absent           -> not present; the stored link is kept.
//   * "" or null           -> present and empty; the stored link is cleared.
//   * any unusable value   -> present and empty, so a bad link is never shown.
//
// ArduinoJson reports a missing key as an unbound variant and a stored null as
// bound (see freezeFieldPresent).
template <typename TJsonObject>
inline VoteUrlSync voteUrlFromJson(const TJsonObject& document,
                                   const char* key) {
    VoteUrlSync sync{};
    const auto value = document[key];
    if (value.isUnbound()) return sync;
    sync.present = true;
    if (!value.template is<const char*>()) return sync;
    const char* text = value.template as<const char*>();
    if (!voteUrlUsable(text)) return sync;
    memcpy(sync.url, text, strlen(text) + 1);
    return sync;
}

#endif  // LOCKBOX_VOTE_LINK_H
