#ifndef LOCKBOX_FREEZE_STATE_H
#define LOCKBOX_FREEZE_STATE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// "Frozen by Chaster" pauses a lock session without ending it. The device stays
// locked, the countdown stops advancing, and the timer can never reach the
// ready-to-unlock page while the freeze holds. The server reports the state as
// isFrozen/frozenAt/freezeReason next to the existing lock fields.
//
// This header is deliberately free of Arduino dependencies so the merge and
// timing rules can be unit-tested natively. See test/test_freeze.

// Bound of the reason persisted in NVS (bytes, including the NUL).
constexpr size_t kFreezeReasonBytes = 64;
// Bound of the reason rendered on the TFT (bytes, including the NUL).
constexpr size_t kFreezeReasonDisplayBytes = 40;
// Bound of the reason rendered on the TFT (characters, before truncation).
constexpr size_t kFreezeReasonDisplayChars = 24;
// Bound of the reason sent over BLE (bytes, including the NUL). The admin frame
// is size-limited, so the reason is truncated here even though the persisted
// copy may be longer.
constexpr size_t kFreezeReasonWireBytes = 48;
constexpr size_t kFreezeReasonWireChars = 46;

// Persisted freeze state. `frozenAt == 0` means the server could not supply an
// anchor: the countdown is still held (never ready, never expiring) and the UI
// shows the generic banner rather than a value it would have to invent.
struct FreezeState {
    bool isFrozen = false;
    uint64_t frozenAt = 0;
    char reason[kFreezeReasonBytes] = "";
};

// Incoming sync fields. The has* flags distinguish "field absent" (a legacy
// payload) from an explicit value, including an explicit null: on the same
// session an absent field must never clear a freeze the device already knows
// about, while an explicit null means the server has no value and is applied.
struct FreezeSync {
    bool hasIsFrozen = false;
    bool isFrozen = false;
    bool hasFrozenAt = false;
    uint64_t frozenAt = 0;
    bool hasReason = false;
    const char* reason = nullptr;
};

struct FreezeMerge {
    // Whether the merged state should be written to NVS.
    bool write = false;
    // Whether the merged state differs from what was already stored.
    bool changed = false;
    FreezeState state{};
};

// A JSON member counts as present when the key exists in the object -- even
// when its value is null. An explicit null is the authoritative "the server has
// no value" answer and must be applied; a *missing* key is a legacy payload and
// must never clear freeze state the device already knows about. ArduinoJson
// reports a missing key as an unbound variant and a stored null as bound.
template <typename TValue>
inline bool freezeFieldPresent(const TValue& value) {
    return !value.isUnbound();
}

// Presence rules shared by the HTTP state parser and the MQTT sync parser.
//
//   * key absent            -> the field is not authoritative. A legacy payload
//                              never clears a known freeze.
//   * isFrozen null/other   -> malformed. Treated as absent so a bad payload
//                              ("false" as a string, null) cannot unfreeze.
//   * frozenAt null/number  -> present. null and 0 both mean "anchor
//                              unavailable", which clears a stale anchor from an
//                              earlier freeze instead of keeping an old epoch.
//   * reason null/string    -> present. null clears the stored reason.
//
// Templated on the JSON object so the exact rules are covered by the native
// tests without pulling in the firmware stack.
template <typename TJsonObject>
inline FreezeSync freezeSyncFromJson(const TJsonObject& document,
                                     const char* isFrozenKey,
                                     const char* frozenAtKey,
                                     const char* reasonKey) {
    FreezeSync sync{};

    const auto frozen = document[isFrozenKey];
    if (freezeFieldPresent(frozen) && frozen.template is<bool>()) {
        sync.hasIsFrozen = true;
        sync.isFrozen = frozen.template as<bool>();
    }

    const auto frozenAt = document[frozenAtKey];
    if (freezeFieldPresent(frozenAt)) {
        sync.hasFrozenAt = true;
        // PostgreSQL numeric timestamps include a decimal suffix. Accept both
        // JSON integer and floating-point numbers, then truncate sub-ms digits.
        // Bounds also reject negative, infinite and non-numeric anchors.
        const double millis = frozenAt.template is<double>()
                                  ? frozenAt.template as<double>()
                                  : 0;
        sync.frozenAt = millis > 0 && millis < 9007199254740992.0
                            ? static_cast<uint64_t>(millis)
                            : 0;
    }

    const auto reason = document[reasonKey];
    if (freezeFieldPresent(reason)) {
        sync.hasReason = true;
        sync.reason = reason.template is<const char*>()
                          ? reason.template as<const char*>()
                          : nullptr;
    }

    return sync;
}

// Length in bytes of the well-formed UTF-8 sequence starting at `s`, or 0 when
// the byte cannot start one: a control byte, a bare continuation byte (what a
// truncated multi-byte sequence leaves behind), an overlong lead or an
// out-of-range lead. Renderers use this to drop malformed bytes instead of
// drawing them, and bounded copies use it to avoid cutting a sequence in half.
inline size_t utf8SequenceLength(const char* s) {
    if (s == nullptr) return 0;
    const unsigned char lead = static_cast<unsigned char>(s[0]);
    if (lead < 0x20 || lead == 0x7F) return 0;  // C0 controls and DEL
    if (lead < 0x80) return 1;                  // plain ASCII
    if (lead < 0xC2) return 0;                  // bare continuation / overlong

    size_t length = 0;
    if (lead < 0xE0) {
        length = 2;
    } else if (lead < 0xF0) {
        length = 3;
    } else if (lead < 0xF5) {
        length = 4;
    } else {
        return 0;  // 0xF5..0xFF are never a valid lead
    }

    for (size_t k = 1; k < length; k++) {
        const unsigned char next = static_cast<unsigned char>(s[k]);
        if (next == '\0' || (next & 0xC0) != 0x80) return 0;
    }
    return length;
}

// Copy `in` into `out`, never writing more than `outSize` bytes including the
// terminator and never splitting a UTF-8 sequence. Malformed bytes are dropped
// rather than persisted, so a truncated reason can never be stored and then
// rendered as mojibake.
inline void copyBoundedString(char* out, size_t outSize, const char* in) {
    if (out == nullptr || outSize == 0) return;
    out[0] = '\0';
    if (in == nullptr) return;

    const size_t limit = outSize - 1;
    size_t written = 0;
    for (size_t index = 0; in[index] != '\0';) {
        const size_t length = utf8SequenceLength(in + index);
        if (length == 0) {
            index++;  // drop a malformed byte
            continue;
        }
        if (written + length > limit) break;
        for (size_t k = 0; k < length; k++) out[written++] = in[index + k];
        index += length;
    }
    out[written] = '\0';
}

inline bool sameFreezeState(const FreezeState& a, const FreezeState& b) {
    return a.isFrozen == b.isFrozen && a.frozenAt == b.frozenAt &&
           strcmp(a.reason, b.reason) == 0;
}

// An anchor or a reason without a freeze is meaningless, so an unfrozen state is
// always the empty state. This keeps a stale epoch from an earlier freeze out of
// the display and out of the BLE snapshot.
inline FreezeState normalizeFreezeState(const FreezeState& state) {
    return state.isFrozen ? state : FreezeState{};
}

// Merge one sync payload into the persisted freeze state.
//
//   newSession - the payload belongs to a different session than the one on
//                disk. That session's namespace is cleared by the caller, so
//                absent fields start fresh instead of preserving the old
//                session's freeze.
//
// Rules:
//   * No freeze field at all (legacy payload): keep the stored state untouched.
//   * Explicit isFrozen:false: clear freeze, anchor and reason.
//   * isFrozen:true keeps the freeze.
//   * frozenAt present: replace the anchor. An explicit null or 0 means the
//     server has no anchor, so a stale epoch from an earlier freeze is dropped
//     instead of being kept. Only an absent frozenAt preserves the anchor.
//   * reason present: replace the stored reason; an explicit null or an empty
//     string clears it. Only an absent reason preserves it.
//   * Any change is what makes the merge worth writing to NVS.
inline FreezeMerge mergeFreezeState(bool newSession, const FreezeState& stored,
                                    const FreezeSync& incoming) {
    const bool anyField = incoming.hasIsFrozen || incoming.hasFrozenAt ||
                          incoming.hasReason;
    const FreezeState base = newSession ? FreezeState{} : stored;

    FreezeMerge result{};

    if (incoming.hasIsFrozen && !incoming.isFrozen) {
        // Explicit unfreeze always wins: clear freeze, anchor and reason.
        result.state = FreezeState{};
    } else if (!anyField) {
        result.state = base;
        return result;
    } else {
        FreezeState next = base;
        if (incoming.hasIsFrozen) next.isFrozen = true;
        if (incoming.hasFrozenAt) next.frozenAt = incoming.frozenAt;
        if (incoming.hasReason)
            copyBoundedString(next.reason, sizeof(next.reason),
                              incoming.reason);
        result.state = normalizeFreezeState(next);
    }

    result.changed = !sameFreezeState(base, result.state);
    result.write = result.changed;
    return result;
}

// True while a freeze holds the countdown. A held timer never expires and never
// reports ready-to-unlock -- including after a reboot, when the deadline is
// already in the past, and while the remaining time is hidden.
inline bool freezeHoldsCountdown(bool isFrozen) { return isFrozen; }

// Whether the server supplied an anchor for the freeze. Without one the UI must
// show the generic banner only: a remaining time recomputed from the clock at
// boot would change across reboots, which is worse than showing no time.
inline bool freezeAnchorKnown(uint64_t frozenAt) { return frozenAt != 0; }

// Whether a held countdown may show its paused value next to the banner. The
// timer must be visible -- a hidden timer is never revealed by a freeze -- and
// the server must have supplied an anchor, otherwise the banner stands alone.
inline bool freezeShowsPausedTime(bool isTimeDisp, uint64_t frozenAt) {
    return isTimeDisp && freezeAnchorKnown(frozenAt);
}

// The paused timer value to render next to the freeze banner: the countdown (or
// the elapsed time, when the wearer shows elapsed) exactly as it stood at
// frozenAt. Both terms are anchored, so the value never ticks while frozen and
// is identical after a reboot. Returns 0 when either anchor is missing.
inline uint64_t frozenPausedDisplayMs(uint64_t frozenAt, uint64_t lockEnd,
                                      uint64_t lockStart,
                                      bool displayElapsedTime) {
    if (frozenAt == 0) return 0;
    if (displayElapsedTime)
        return lockStart != 0 && frozenAt > lockStart ? frozenAt - lockStart
                                                     : 0;
    return frozenAt >= lockEnd ? 0 : lockEnd - frozenAt;
}

// Remaining lock time for a held countdown, anchored at the freeze. The value
// stops advancing for the whole freeze and is identical across reboots. An
// anchor at or past the deadline is a valid expired anchor and reports 0; an
// unknown anchor (0, or a missing deadline) reports 0 too, and callers that
// must distinguish "unknown" check freezeAnchorKnown() first. Never underflows:
// both terms are compared before subtracting.
inline uint64_t frozenRemainingAtAnchor(uint64_t frozenAt, uint64_t lockEnd) {
    if (frozenAt == 0 || lockEnd == 0) return 0;
    return lockEnd > frozenAt ? lockEnd - frozenAt : 0;
}

// Copy a server-supplied reason into a fixed display buffer. Control bytes are
// dropped, the UTF-8 sequence length is honoured, and the copy is truncated
// (with an ASCII ellipsis when it fits) so a long or hostile reason can never
// overrun the TFT text buffer. Returns the number of bytes written, excluding
// the terminator.
inline size_t boundedFreezeReason(const char* reason, char* out,
                                  size_t outCapacity,
                                  size_t maxChars = kFreezeReasonDisplayChars) {
    if (out == nullptr || outCapacity == 0) return 0;
    out[0] = '\0';
    if (reason == nullptr) return 0;

    const size_t limit = outCapacity - 1;
    size_t written = 0;
    size_t chars = 0;
    size_t index = 0;
    bool truncated = false;

    while (reason[index] != '\0') {
        const size_t length = utf8SequenceLength(reason + index);
        if (length == 0) {
            // Drop control bytes, bare continuation bytes and other malformed
            // input rather than rendering them.
            index++;
            continue;
        }
        if (chars == maxChars || written + length > limit) {
            truncated = true;
            break;
        }

        for (size_t k = 0; k < length; k++) out[written++] = reason[index + k];
        chars++;
        index += length;
    }

    if (truncated && written + 3 <= limit) {
        out[written++] = '.';
        out[written++] = '.';
        out[written++] = '.';
    }
    out[written] = '\0';
    return written;
}

// Display refresh hint. The unlock-timer task polls this so a freeze-only sync
// repaints immediately instead of waiting for its periodic refresh. It is only
// a hint -- the periodic re-read remains the guarantee -- so a plain 32-bit
// read/write is sufficient and deliberately not mutex-protected.
inline volatile uint32_t& freezeDisplayVersion() {
    static volatile uint32_t version = 0;
    return version;
}

inline void bumpFreezeDisplayVersion() {
    freezeDisplayVersion() = freezeDisplayVersion() + 1;
}

#endif  // LOCKBOX_FREEZE_STATE_H
