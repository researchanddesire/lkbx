#ifndef LOCKBOX_CHASTER_ASK_H
#define LOCKBOX_CHASTER_ASK_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "breakTiming.h"
#include "freezeState.h"

// While Chaster controls a session, Chaster decides when the box may open. The
// box therefore asks the dashboard before it opens for a break or unlocks at
// the end of its timer, and it opens only on an answer that says so.
//
//   POST {RAD_SERVER}/api/lockbox/v2/ask   {"deviceToken", "sessionId", "action"}
//   POST {RAD_SERVER}/api/lockbox/ask      {"macAddress", "deviceSecret",
//                                           "sessionId", "action"}
//   200  {"ok": true, "sessionId", "action", "allowed", "code", "reason"}
//
// `action` is "break" or "unlock". The answer must repeat the session and the
// action it answers. Anything else (no Wi-Fi, a timeout, any other status, a
// malformed body, an answer for another question) is no answer, and the box
// stays locked: offline, a Chaster session never opens on its own timer.
// Emergency unlock and a clear from the dashboard do not ask.
//
// This header has no Arduino dependencies so the rules are unit-tested
// natively. See test/test_chaster_ask.

namespace chasterAsk {
    constexpr char kOk[] = "ok";
    constexpr char kAllowed[] = "allowed";
    constexpr char kSessionId[] = "sessionId";
    constexpr char kAction[] = "action";
    constexpr char kCode[] = "code";
    constexpr char kReason[] = "reason";
    // The dashboard holds no such session for this box: its state is read
    // again instead, which is what ends a session the box missed the end of.
    constexpr char kCodeNoSession[] = "no_session";
    // Chaster could not be asked. Not a refusal: the wearer may ask again.
    constexpr char kCodeUnavailable[] = "unavailable";
    // The whole request, including queue time and the TLS handshake. The
    // dashboard itself waits up to 8 s for Chaster.
    constexpr int kTimeoutMs = 20000;
    // Handing the question to the HTTP worker can fail for a moment: its
    // queue is full, or there is no memory for the request. The box tries
    // once more after a short wait before it reports that it could not ask.
    constexpr uint8_t kSendAttempts = 2;
    constexpr uint32_t kSendRetryMs = 500;
}  // namespace chasterAsk

enum class AskAction : uint8_t { BREAK, UNLOCK };

inline const char* askActionText(AskAction action) {
    return action == AskAction::BREAK ? "break" : "unlock";
}

// The question is asked from the delay task, which always exists. A task of
// its own needed one free block of internal RAM as large as its stack, and R8
// hardware often has none that large: the box then could not ask at all and
// showed "Can't Reach Chaster" with Wi-Fi up.
//
// One word names the newest question: its number and what it asks. The
// transition into "locked.asking" writes the word and the delay task reads
// it, so the two always agree on which question is the newest and what it is.
// An answer counts only while its question is still the newest.
constexpr uint32_t kNoAskOrder = 0;

inline uint32_t nextAskOrder(uint32_t previous, AskAction action) {
    uint32_t number = ((previous >> 1) + 1) & 0x7FFFFFFFu;
    // Never the value that means "nothing was asked".
    if (number == 0) number = 1;
    return (number << 1) | (action == AskAction::BREAK ? 1u : 0u);
}

inline AskAction askOrderAction(uint32_t order) {
    return (order & 1u) != 0 ? AskAction::BREAK : AskAction::UNLOCK;
}

// Whether a question the HTTP worker could not take is handed over again.
inline bool askSendMayRetry(uint8_t attemptsMade) {
    return attemptsMade < chasterAsk::kSendAttempts;
}

enum class AskOutcome : uint8_t {
    GRANTED,        // open
    REFUSED,        // Chaster said no; `reason` says why
    UNANSWERED,     // nobody could be asked: stay locked
    STALE_SESSION,  // the dashboard does not know this session: re-read state
};

// Bound of the reason shown on the answer page (bytes, including the NUL).
constexpr size_t kAskReasonBytes = 96;

struct AskAnswer {
    AskOutcome outcome = AskOutcome::UNANSWERED;
    char reason[kAskReasonBytes] = "";
};

// Reads the dashboard's answer. Fails closed: only a complete 200 that names
// the asked session and action can grant anything.
template <typename TJsonObject>
inline AskAnswer classifyAskAnswer(int httpStatus, const TJsonObject& body,
                                   const char* sessionId, AskAction action) {
    AskAnswer answer{};
    if (httpStatus != 200 || sessionId == nullptr || sessionId[0] == '\0') {
        return answer;
    }

    const auto ok = body[chasterAsk::kOk];
    const auto allowed = body[chasterAsk::kAllowed];
    if (!ok.template is<bool>() || !ok.template as<bool>() ||
        !allowed.template is<bool>()) {
        return answer;
    }

    const auto answeredSession = body[chasterAsk::kSessionId];
    const auto answeredAction = body[chasterAsk::kAction];
    if (!answeredSession.template is<const char*>() ||
        !answeredAction.template is<const char*>() ||
        strcmp(answeredSession.template as<const char*>(), sessionId) != 0 ||
        strcmp(answeredAction.template as<const char*>(),
               askActionText(action)) != 0) {
        return answer;
    }

    const auto reason = body[chasterAsk::kReason];
    copyBoundedString(answer.reason, sizeof(answer.reason),
                      reason.template is<const char*>()
                          ? reason.template as<const char*>()
                          : nullptr);

    if (allowed.template as<bool>()) {
        answer.outcome = AskOutcome::GRANTED;
        return answer;
    }

    const auto code = body[chasterAsk::kCode];
    const char* codeText =
        code.template is<const char*>() ? code.template as<const char*>() : "";
    if (strcmp(codeText, chasterAsk::kCodeNoSession) == 0) {
        answer.outcome = AskOutcome::STALE_SESSION;
    } else if (strcmp(codeText, chasterAsk::kCodeUnavailable) == 0) {
        answer.outcome = AskOutcome::UNANSWERED;
    } else {
        answer.outcome = AskOutcome::REFUSED;
    }
    return answer;
}

// What a granted unlock does when the answer lands. The question may have been
// overtaken (the wearer was moved off the asking page by the dashboard), and
// the session is checked once more: time may have been added, or a freeze may
// have landed, while the box was asking.
enum class UnlockGrant : uint8_t {
    UNLOCK,  // open
    RETURN,  // no longer ready to unlock: back to the lock page
    DROP,    // nobody is waiting for this answer
};

inline UnlockGrant unlockGrantStep(bool stillAsking, bool sessionStillReady) {
    if (!stillAsking) return UnlockGrant::DROP;
    return sessionStillReady ? UnlockGrant::UNLOCK : UnlockGrant::RETURN;
}

// Whether Chaster controls the session after a sync. A payload that leaves the
// field out (older dashboard, BLE session write) never turns asking off for
// the session the box already holds; a new session starts without it.
inline bool mergeChasterControlled(bool newSession, bool stored,
                                   bool hasIncoming, bool incoming) {
    if (hasIncoming) return incoming;
    return newSession ? false : stored;
}

// Reads one boolean flag from a sync payload. False when the key is absent or
// is not a boolean, which leaves the stored value alone.
template <typename TJsonObject>
inline bool sessionFlagFromJson(const TJsonObject& document, const char* key,
                                bool& value) {
    const auto field = document[key];
    if (!freezeFieldPresent(field) || !field.template is<bool>()) return false;
    value = field.template as<bool>();
    return true;
}

// The dashboard says "ask before opening" with two fields: `chasterControlled`
// and the older `requiresPermission`. Either one being true makes the box ask;
// the payload carries the answer when it carries either field.
struct AskFlag {
    bool present = false;
    bool asks = false;
};

inline AskFlag combineAskFlags(bool hasChasterControlled, bool chasterControlled,
                               bool hasRequiresPermission,
                               bool requiresPermission) {
    AskFlag flag{};
    flag.present = hasChasterControlled || hasRequiresPermission;
    flag.asks = (hasChasterControlled && chasterControlled) ||
                (hasRequiresPermission && requiresPermission);
    return flag;
}

// The dashboard publishes "Chaster would refuse this unlock" through the freeze
// fields, with a reason that starts with this label. The box shows that as
// waiting, not as frozen.
constexpr char kUnlockHoldLabel[] = "Waiting for Chaster";

// The text after the label, or nullptr when the reason is not an unlock hold.
// An unlock hold without a detail returns an empty string.
inline const char* unlockHoldDetail(const char* reason) {
    if (reason == nullptr) return nullptr;
    const size_t labelLength = sizeof(kUnlockHoldLabel) - 1;
    if (strncmp(reason, kUnlockHoldLabel, labelLength) != 0) return nullptr;
    const char* detail = reason + labelLength;
    while (*detail == ':' || *detail == ' ') detail++;
    return detail;
}

inline bool isUnlockHoldReason(const char* reason) {
    return unlockHoldDetail(reason) != nullptr;
}

// The reason line under the held-session heading: the detail of an unlock
// hold, the whole reason of a freeze.
inline const char* heldReasonLine(const char* reason) {
    const char* detail = unlockHoldDetail(reason);
    return detail != nullptr ? detail : reason;
}

// While the timer is hidden, nothing on the screen may depend on the time
// left. The dashboard can publish an unlock hold shortly before the end of the
// timer; showing it then would tell the wearer the end is near. So with a
// hidden timer an unlock hold gets its own page only once the box's own
// deadline has passed. Until then the ordinary locked page stays up. The hold
// still holds either way: a held session never turns ready (isReadyToUnlock
// reads the stored flag, not this).
//
// A real freeze is always shown: it does not depend on the time left.
inline bool holdShownOnScreen(bool isFrozen, bool isUnlockHold,
                              bool isTimeDisp, bool clockTrusted,
                              uint64_t lockEnd, uint64_t now) {
    if (!isFrozen) return false;
    if (isTimeDisp || !isUnlockHold) return true;
    // No deadline to give away.
    if (lockEnd == 0) return true;
    // Without a trusted clock the box cannot tell whether the deadline has
    // passed, so it shows nothing new.
    if (!clockTrusted) return false;
    return now >= lockEnd;
}

// The screen wakes for the last seconds of a visible countdown. A hidden timer
// must not: a screen that lights up on its own says the end is near.
inline bool wakesScreenNearExpiry(bool isTimeDisp, bool held,
                                  uint64_t remainingMs) {
    return isTimeDisp && !held && remainingMs > 0 && remainingMs < 30000;
}

// Choosing Sleep close to the end of a visible timer returns to the countdown
// instead. With a hidden timer the Sleep item behaves the same at any time.
inline bool sleepReturnsToTimer(bool isTimeDisp, bool held, bool nearExpiry) {
    return isTimeDisp && !held && nearExpiry;
}

// A break a Chaster session is on ends when Chaster, the keyholder or the
// wearer ends it, never because the box's own deadline passed: on a Chaster
// session that deadline does not decide anything.
inline bool breakEndsAtLockWindow(bool chasterControlled, bool clockTrusted,
                                  uint64_t lockEnd, uint64_t now) {
    return !chasterControlled && clockTrusted && isLockWindowOver(lockEnd, now);
}

#endif  // LOCKBOX_CHASTER_ASK_H
