#include <ArduinoJson.h>

#include "unity.h"

#include "../../src/utils/freezeState.h"

void setUp() {}
void tearDown() {}

namespace {

FreezeState frozenState(bool isFrozen, uint64_t frozenAt,
                        const char* reason) {
    FreezeState state{};
    state.isFrozen = isFrozen;
    state.frozenAt = frozenAt;
    copyBoundedString(state.reason, sizeof(state.reason), reason);
    return state;
}

}  // namespace

// --- legacy payloads --------------------------------------------------------

void test_legacy_payload_preserves_freeze_for_the_same_session() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    const FreezeSync legacy{};  // no isFrozen/frozenAt/freezeReason at all

    const FreezeMerge merged = mergeFreezeState(false, stored, legacy);

    TEST_ASSERT_FALSE(merged.write);
    TEST_ASSERT_FALSE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(1789188000000ULL, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("Keyholder", merged.state.reason);
}

void test_unfrozen_session_ignores_a_legacy_payload() {
    const FreezeState stored{};
    const FreezeSync legacy{};

    const FreezeMerge merged = mergeFreezeState(false, stored, legacy);

    TEST_ASSERT_FALSE(merged.write);
    TEST_ASSERT_FALSE(merged.changed);
    TEST_ASSERT_FALSE(merged.state.isFrozen);
}

void test_new_session_starts_fresh_when_freeze_fields_are_absent() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    const FreezeSync legacy{};

    const FreezeMerge merged = mergeFreezeState(true, stored, legacy);

    TEST_ASSERT_FALSE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(0, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("", merged.state.reason);
    TEST_ASSERT_FALSE(merged.changed);
}

// --- explicit unfreeze ------------------------------------------------------

void test_explicit_unfreeze_clears_freeze_anchor_and_reason() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    FreezeSync incoming{};
    incoming.hasIsFrozen = true;
    incoming.isFrozen = false;

    const FreezeMerge merged = mergeFreezeState(false, stored, incoming);

    TEST_ASSERT_TRUE(merged.write);
    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_FALSE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(0, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("", merged.state.reason);
}

void test_unfreeze_of_an_already_unfrozen_session_reports_no_change() {
    const FreezeState stored{};
    FreezeSync incoming{};
    incoming.hasIsFrozen = true;
    incoming.isFrozen = false;

    const FreezeMerge merged = mergeFreezeState(false, stored, incoming);

    TEST_ASSERT_FALSE(merged.changed);
    TEST_ASSERT_FALSE(merged.state.isFrozen);
}

// --- freeze merges ----------------------------------------------------------

void test_freeze_carries_anchor_and_reason() {
    const FreezeState stored{};
    FreezeSync incoming{};
    incoming.hasIsFrozen = true;
    incoming.isFrozen = true;
    incoming.hasFrozenAt = true;
    incoming.frozenAt = 1789188000000ULL;
    incoming.hasReason = true;
    incoming.reason = "Keyholder froze the session";

    const FreezeMerge merged = mergeFreezeState(false, stored, incoming);

    TEST_ASSERT_TRUE(merged.write);
    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(1789188000000ULL, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("Keyholder froze the session", merged.state.reason);
}

void test_missing_fields_keep_known_anchor_and_reason() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    FreezeSync incoming{};
    incoming.hasIsFrozen = true;
    incoming.isFrozen = true;

    const FreezeMerge merged = mergeFreezeState(false, stored, incoming);

    TEST_ASSERT_FALSE(merged.changed);  // nothing new arrived
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(1789188000000ULL, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("Keyholder", merged.state.reason);
}

void test_explicit_zero_anchor_clears_a_stale_timestamp() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    FreezeSync incoming{};
    incoming.hasIsFrozen = true;
    incoming.isFrozen = true;
    incoming.hasFrozenAt = true;
    incoming.frozenAt = 0;  // "0 if unavailable"

    const FreezeMerge merged = mergeFreezeState(false, stored, incoming);

    // An explicit "no anchor" must not keep the previous freeze's epoch: the
    // device would otherwise show a remaining time from a different freeze.
    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(0, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("Keyholder", merged.state.reason);
}

void test_explicit_null_reason_clears_a_previous_reason() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    FreezeSync incoming{};
    incoming.hasIsFrozen = true;
    incoming.isFrozen = true;
    incoming.hasReason = true;
    incoming.reason = nullptr;  // explicit freezeReason:null

    const FreezeMerge merged = mergeFreezeState(false, stored, incoming);

    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(1789188000000ULL, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("", merged.state.reason);
}

void test_unfrozen_state_never_keeps_an_anchor_or_reason() {
    // Defensive: an anchor without a freeze is meaningless, so it is dropped.
    const FreezeState normalized = normalizeFreezeState(
        frozenState(false, 1789188000000ULL, "Keyholder"));
    TEST_ASSERT_FALSE(normalized.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(0, normalized.frozenAt);
    TEST_ASSERT_EQUAL_STRING("", normalized.reason);
}

void test_late_freeze_without_an_anchor_still_sets_the_freeze() {
    const FreezeState stored{};
    FreezeSync incoming{};
    incoming.hasIsFrozen = true;
    incoming.isFrozen = true;

    const FreezeMerge merged = mergeFreezeState(false, stored, incoming);

    TEST_ASSERT_TRUE(merged.write);
    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(0, merged.state.frozenAt);
}

void test_empty_reason_clears_only_the_reason() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    FreezeSync incoming{};
    incoming.hasIsFrozen = true;
    incoming.isFrozen = true;
    incoming.hasReason = true;
    incoming.reason = "";

    const FreezeMerge merged = mergeFreezeState(false, stored, incoming);

    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(1789188000000ULL, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("", merged.state.reason);
}

// --- held countdown ---------------------------------------------------------

void test_anchored_remaining_stops_advancing_at_the_freeze() {
    const uint64_t lockEnd = 1789191600000ULL;
    const uint64_t frozenAt = 1789188000000ULL;

    // 1 hour held at the anchor, no matter how long the freeze lasts.
    TEST_ASSERT_EQUAL_UINT64(3600000ULL,
                             frozenRemainingAtAnchor(frozenAt, lockEnd));
    TEST_ASSERT_EQUAL_UINT64(3600000ULL,
                             frozenRemainingAtAnchor(frozenAt, lockEnd));
}

void test_remaining_never_underflows_or_overflows() {
    TEST_ASSERT_EQUAL_UINT64(
        0, frozenRemainingAtAnchor(1789188000000ULL, 1789188000000ULL));
    // Frozen after (or exactly at) the deadline: zero remaining, still held.
    TEST_ASSERT_EQUAL_UINT64(
        0, frozenRemainingAtAnchor(1789188000001ULL, 1789188000000ULL));
    TEST_ASSERT_EQUAL_UINT64(0, frozenRemainingAtAnchor(UINT64_MAX, UINT64_MAX));
    TEST_ASSERT_EQUAL_UINT64(UINT64_MAX - 1,
                             frozenRemainingAtAnchor(1, UINT64_MAX));
}

void test_freeze_holds_the_countdown_past_the_deadline() {
    const uint64_t lockEnd = 1789188000000ULL;
    const uint64_t frozenAt = 1789189000000ULL;  // frozen after the deadline

    // The freeze still holds: zero remaining is displayed and the session must
    // never be treated as ready, including after a reboot into this state.
    TEST_ASSERT_TRUE(freezeHoldsCountdown(true));
    TEST_ASSERT_EQUAL_UINT64(0, frozenRemainingAtAnchor(frozenAt, lockEnd));
}

void test_missing_anchor_or_deadline_reports_no_remaining_time() {
    // No anchor: nothing may be computed from the clock, so the countdown
    // cannot run to ready and the UI shows the generic banner.
    TEST_ASSERT_EQUAL_UINT64(0,
                             frozenRemainingAtAnchor(0, 1789191600000ULL));
    // A missing deadline is unknown too: never report ready.
    TEST_ASSERT_EQUAL_UINT64(0,
                             frozenRemainingAtAnchor(1789188000000ULL, 0));
}

void test_paused_display_time_is_anchored_and_never_ticks() {
    const uint64_t lockStart = 1789184400000ULL;
    const uint64_t lockEnd = 1789191600000ULL;
    const uint64_t frozenAt = 1789188000000ULL;

    // The countdown stops one hour before the deadline...
    TEST_ASSERT_EQUAL_UINT64(
        3600000ULL,
        frozenPausedDisplayMs(frozenAt, lockEnd, lockStart, false));
    // ...and the elapsed display stops one hour into the lock. Repeating the
    // call returns the same value: nothing advances while frozen.
    TEST_ASSERT_EQUAL_UINT64(
        3600000ULL,
        frozenPausedDisplayMs(frozenAt, lockEnd, lockStart, true));
    TEST_ASSERT_EQUAL_UINT64(
        3600000ULL,
        frozenPausedDisplayMs(frozenAt, lockEnd, lockStart, false));
}

void test_paused_display_time_is_unknown_without_an_anchor() {
    TEST_ASSERT_FALSE(freezeAnchorKnown(0));
    TEST_ASSERT_TRUE(freezeAnchorKnown(1789188000000ULL));

    // No anchor: the UI shows the generic banner, never a synthesised value that
    // would differ after a reboot.
    TEST_ASSERT_EQUAL_UINT64(
        0, frozenPausedDisplayMs(0, 1789191600000ULL, 1789184400000ULL, false));
    TEST_ASSERT_EQUAL_UINT64(
        0, frozenPausedDisplayMs(0, 1789191600000ULL, 1789184400000ULL, true));
    // No lock start: the elapsed form has nothing to anchor to.
    TEST_ASSERT_EQUAL_UINT64(
        0, frozenPausedDisplayMs(1789188000000ULL, 1789191600000ULL, 0, true));
}

void test_hidden_timer_is_never_revealed_while_frozen() {
    // A frozen session with an anchor shows the paused value for a visible
    // timer...
    TEST_ASSERT_TRUE(freezeShowsPausedTime(true, 1789188000000ULL));
    // ...but a hidden timer keeps hiding it, with or without an anchor. This is
    // the rule the countdown task uses to decide whether to draw a time at all.
    TEST_ASSERT_FALSE(freezeShowsPausedTime(false, 1789188000000ULL));
    TEST_ASSERT_FALSE(freezeShowsPausedTime(false, 0));
    // No anchor: the banner stands alone.
    TEST_ASSERT_FALSE(freezeShowsPausedTime(true, 0));
}

void test_expired_anchor_reports_zero_not_unknown() {
    const uint64_t lockEnd = 1789191600000ULL;

    // Frozen at (or after) the deadline: the session is expired but still
    // frozen, so the remaining time is a known 0 rather than unknown.
    TEST_ASSERT_EQUAL_UINT64(0, frozenRemainingAtAnchor(lockEnd, lockEnd));
    TEST_ASSERT_EQUAL_UINT64(0,
                             frozenRemainingAtAnchor(lockEnd + 1, lockEnd));
    TEST_ASSERT_EQUAL_UINT64(0, frozenPausedDisplayMs(lockEnd, lockEnd, 0, false));
    // An anchor inside the session still reports the paused remainder.
    TEST_ASSERT_EQUAL_UINT64(1000ULL,
                             frozenRemainingAtAnchor(lockEnd - 1000, lockEnd));
}

// --- display bounding -------------------------------------------------------

void test_bounded_reason_strips_control_bytes() {
    char out[kFreezeReasonDisplayBytes];
    const size_t written =
        boundedFreezeReason("Frozen\n\tby Chaster\r", out, sizeof(out));

    TEST_ASSERT_EQUAL_STRING("Frozenby Chaster", out);
    TEST_ASSERT_EQUAL_UINT32(16, written);
}

void test_bounded_reason_truncates_long_text() {
    char out[kFreezeReasonDisplayBytes];
    const char* longReason =
        "This reason is far too long to render on the lockbox display";
    const size_t written = boundedFreezeReason(longReason, out, sizeof(out));

    TEST_ASSERT_TRUE(written < strlen(longReason));
    TEST_ASSERT_TRUE(written <= sizeof(out) - 1);
    TEST_ASSERT_EQUAL_STRING("This reason is far too l...", out);
}

void test_bounded_reason_never_overruns_a_small_buffer() {
    char out[6];
    const size_t written = boundedFreezeReason("abcdefghij", out, sizeof(out));

    TEST_ASSERT_EQUAL_UINT32(5, written);
    TEST_ASSERT_EQUAL_STRING("abcde", out);
    TEST_ASSERT_EQUAL_UINT32(5, strlen(out));
}

void test_bounded_reason_keeps_utf8_sequences_intact() {
    char out[kFreezeReasonDisplayBytes];
    boundedFreezeReason("Fr\303\266zen", out, sizeof(out));

    TEST_ASSERT_EQUAL_STRING("Fr\303\266zen", out);
}

void test_bounded_reason_handles_empty_and_missing() {
    char out[kFreezeReasonDisplayBytes];

    TEST_ASSERT_EQUAL_UINT32(0, boundedFreezeReason(nullptr, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_UINT32(0, boundedFreezeReason("", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    TEST_ASSERT_EQUAL_UINT32(0, boundedFreezeReason("ignored", out, 0));
}

void test_bounded_reason_drops_malformed_continuation_bytes() {
    char out[kFreezeReasonDisplayBytes];

    // A persisted copy that was cut mid-sequence leaves a lead byte followed by
    // one continuation byte; neither may be rendered on the TFT.
    boundedFreezeReason("Fr\342\202", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("Fr", out);

    // Bare continuation bytes and control bytes are never rendered either.
    boundedFreezeReason("\200\201abc", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("abc", out);
}

void test_bounded_reason_honours_a_larger_wire_budget() {
    char out[kFreezeReasonWireBytes];
    const char* longer = "This reason fits the admin wire budget";

    const size_t written =
        boundedFreezeReason(longer, out, sizeof(out), kFreezeReasonWireChars);

    TEST_ASSERT_TRUE(written <= sizeof(out) - 1);
    TEST_ASSERT_EQUAL_STRING(longer, out);
}

void test_bounded_copy_never_splits_a_utf8_sequence() {
    char out[5];
    copyBoundedString(out, sizeof(out), "abc\303\266");  // "abc" + o-umlaut
    TEST_ASSERT_EQUAL_STRING("abc", out);

    char roomy[8];
    copyBoundedString(roomy, sizeof(roomy), "abc\303\266");
    TEST_ASSERT_EQUAL_STRING("abc\303\266", roomy);
}

void test_bounded_copy_truncates_on_a_sequence_boundary() {
    char out[8];
    copyBoundedString(out, sizeof(out), "ab\303\266\303\266\303\266");

    TEST_ASSERT_EQUAL_STRING("ab\303\266\303\266", out);
    TEST_ASSERT_EQUAL_UINT32(6, strlen(out));
}

void test_bounded_copy_drops_control_bytes() {
    char out[kFreezeReasonBytes];
    copyBoundedString(out, sizeof(out), "Frozen\nby Chaster\r");
    TEST_ASSERT_EQUAL_STRING("Frozenby Chaster", out);
}

// --- HTTP / MQTT payload presence ------------------------------------------

void test_absent_freeze_fields_are_not_present() {
    JsonDocument doc;

    deserializeJson(doc, "{}");
    FreezeSync sync =
        freezeSyncFromJson(doc, "isFrozen", "frozenAt", "freezeReason");
    TEST_ASSERT_FALSE(sync.hasIsFrozen);
    TEST_ASSERT_FALSE(sync.hasFrozenAt);
    TEST_ASSERT_FALSE(sync.hasReason);
}

void test_postgres_decimal_milliseconds_keep_the_freeze_anchor() {
    JsonDocument doc;
    // PostgreSQL's extract(epoch) * 1000 serializes numeric values with a
    // decimal suffix even when the timestamp is an exact millisecond.
    deserializeJson(doc, "{\"isFrozen\":true,\"frozenAt\":1789188000123.000000}");
    const auto sync = freezeSyncFromJson(doc, "isFrozen", "frozenAt", "freezeReason");
    TEST_ASSERT_EQUAL_UINT64(1789188000123ULL, sync.frozenAt);
}

void test_invalid_numeric_anchors_are_unknown() {
    for (const char* payload : {"{\"frozenAt\":-123.0}", "{\"frozenAt\":1e30}",
                                "{\"frozenAt\":\"1789188000123\"}", "{\"frozenAt\":true}"}) {
        JsonDocument doc;
        deserializeJson(doc, payload);
        const auto sync = freezeSyncFromJson(doc, "isFrozen", "frozenAt", "freezeReason");
        TEST_ASSERT_EQUAL_UINT64(0, sync.frozenAt);
    }
}

void test_explicit_nulls_are_present_and_authoritative() {
    JsonDocument doc;
    deserializeJson(doc,
                    "{\"isFrozen\":null,\"frozenAt\":null,\"freezeReason\":null}");
    const FreezeSync sync =
        freezeSyncFromJson(doc, "isFrozen", "frozenAt", "freezeReason");

    // A null isFrozen is not a boolean, so it can never clear a freeze...
    TEST_ASSERT_FALSE(sync.hasIsFrozen);
    // ...while a null anchor and a null reason are the server telling the device
    // it has no value, which clears the stale ones on the same session.
    TEST_ASSERT_TRUE(sync.hasFrozenAt);
    TEST_ASSERT_EQUAL_UINT64(0, sync.frozenAt);
    TEST_ASSERT_TRUE(sync.hasReason);
    TEST_ASSERT_EQUAL_STRING("", sync.reason != nullptr ? sync.reason : "");
}

void test_malformed_is_frozen_cannot_clear_a_known_freeze() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    JsonDocument doc;

    // A string that looks like a boolean is not one.
    deserializeJson(doc, "{\"isFrozen\":\"false\"}");
    FreezeSync sync =
        freezeSyncFromJson(doc, "isFrozen", "frozenAt", "freezeReason");
    TEST_ASSERT_FALSE(sync.hasIsFrozen);
    FreezeMerge merged = mergeFreezeState(false, stored, sync);
    TEST_ASSERT_FALSE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(1789188000000ULL, merged.state.frozenAt);

    // Neither is a number, nor a literal null.
    deserializeJson(doc, "{\"isFrozen\":0}");
    sync = freezeSyncFromJson(doc, "isFrozen", "frozenAt", "freezeReason");
    TEST_ASSERT_FALSE(sync.hasIsFrozen);

    deserializeJson(doc, "{\"isFrozen\":null}");
    sync = freezeSyncFromJson(doc, "isFrozen", "frozenAt", "freezeReason");
    TEST_ASSERT_FALSE(sync.hasIsFrozen);

    merged = mergeFreezeState(false, stored, sync);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(1789188000000ULL, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("Keyholder", merged.state.reason);
}

void test_json_null_anchor_and_reason_clear_stale_values() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    JsonDocument doc;
    deserializeJson(
        doc, "{\"isFrozen\":true,\"frozenAt\":null,\"freezeReason\":null}");

    const FreezeMerge merged =
        mergeFreezeState(false, stored,
                         freezeSyncFromJson(doc, "isFrozen", "frozenAt",
                                            "freezeReason"));

    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(0, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("", merged.state.reason);
}

void test_json_zero_anchor_clears_a_stale_timestamp() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    JsonDocument doc;
    deserializeJson(doc, "{\"isFrozen\":true,\"frozenAt\":0}");

    const FreezeMerge merged =
        mergeFreezeState(false, stored,
                         freezeSyncFromJson(doc, "isFrozen", "frozenAt",
                                            "freezeReason"));

    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_EQUAL_UINT64(0, merged.state.frozenAt);
}

void test_http_shaped_payload_freezes_and_unfreezes() {
    const FreezeState stored{};
    JsonDocument doc;
    const char* payload =
        "{\"lockState\":\"locked\",\"isFrozen\":true,"
        "\"frozenAt\":1789188000000,\"freezeReason\":\"Keyholder\"}";
    deserializeJson(doc, payload);

    FreezeMerge merged =
        mergeFreezeState(false, stored,
                         freezeSyncFromJson(doc, "isFrozen", "frozenAt",
                                            "freezeReason"));
    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(1789188000000ULL, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("Keyholder", merged.state.reason);

    deserializeJson(doc, "{\"lockState\":\"locked\",\"isFrozen\":false}");
    merged = mergeFreezeState(false, merged.state,
                              freezeSyncFromJson(doc, "isFrozen", "frozenAt",
                                                 "freezeReason"));
    TEST_ASSERT_TRUE(merged.changed);
    TEST_ASSERT_FALSE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(0, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("", merged.state.reason);
}

void test_mqtt_shaped_payload_without_freeze_keeps_the_freeze() {
    const FreezeState stored = frozenState(true, 1789188000000ULL, "Keyholder");
    JsonDocument doc;
    // Legacy sync message: no freeze fields at all.
    deserializeJson(doc,
                    "{\"type\":\"sync\",\"lockState\":\"locked\","
                    "\"lockEnd\":1789191600000,\"sessionId\":\"abc\"}");

    const FreezeMerge merged =
        mergeFreezeState(false, stored,
                         freezeSyncFromJson(doc, "isFrozen", "frozenAt",
                                            "freezeReason"));

    TEST_ASSERT_FALSE(merged.write);
    TEST_ASSERT_FALSE(merged.changed);
    TEST_ASSERT_TRUE(merged.state.isFrozen);
    TEST_ASSERT_EQUAL_UINT64(1789188000000ULL, merged.state.frozenAt);
    TEST_ASSERT_EQUAL_STRING("Keyholder", merged.state.reason);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_legacy_payload_preserves_freeze_for_the_same_session);
    RUN_TEST(test_unfrozen_session_ignores_a_legacy_payload);
    RUN_TEST(test_new_session_starts_fresh_when_freeze_fields_are_absent);
    RUN_TEST(test_explicit_unfreeze_clears_freeze_anchor_and_reason);
    RUN_TEST(test_unfreeze_of_an_already_unfrozen_session_reports_no_change);
    RUN_TEST(test_freeze_carries_anchor_and_reason);
    RUN_TEST(test_missing_fields_keep_known_anchor_and_reason);
    RUN_TEST(test_explicit_zero_anchor_clears_a_stale_timestamp);
    RUN_TEST(test_explicit_null_reason_clears_a_previous_reason);
    RUN_TEST(test_unfrozen_state_never_keeps_an_anchor_or_reason);
    RUN_TEST(test_late_freeze_without_an_anchor_still_sets_the_freeze);
    RUN_TEST(test_empty_reason_clears_only_the_reason);
    RUN_TEST(test_anchored_remaining_stops_advancing_at_the_freeze);
    RUN_TEST(test_remaining_never_underflows_or_overflows);
    RUN_TEST(test_freeze_holds_the_countdown_past_the_deadline);
    RUN_TEST(test_missing_anchor_or_deadline_reports_no_remaining_time);
    RUN_TEST(test_paused_display_time_is_anchored_and_never_ticks);
    RUN_TEST(test_paused_display_time_is_unknown_without_an_anchor);
    RUN_TEST(test_hidden_timer_is_never_revealed_while_frozen);
    RUN_TEST(test_expired_anchor_reports_zero_not_unknown);
    RUN_TEST(test_bounded_reason_strips_control_bytes);
    RUN_TEST(test_bounded_reason_truncates_long_text);
    RUN_TEST(test_bounded_reason_never_overruns_a_small_buffer);
    RUN_TEST(test_bounded_reason_keeps_utf8_sequences_intact);
    RUN_TEST(test_bounded_reason_handles_empty_and_missing);
    RUN_TEST(test_bounded_reason_drops_malformed_continuation_bytes);
    RUN_TEST(test_bounded_reason_honours_a_larger_wire_budget);
    RUN_TEST(test_bounded_copy_never_splits_a_utf8_sequence);
    RUN_TEST(test_bounded_copy_truncates_on_a_sequence_boundary);
    RUN_TEST(test_bounded_copy_drops_control_bytes);
    RUN_TEST(test_absent_freeze_fields_are_not_present);
    RUN_TEST(test_postgres_decimal_milliseconds_keep_the_freeze_anchor);
    RUN_TEST(test_invalid_numeric_anchors_are_unknown);
    RUN_TEST(test_explicit_nulls_are_present_and_authoritative);
    RUN_TEST(test_malformed_is_frozen_cannot_clear_a_known_freeze);
    RUN_TEST(test_json_null_anchor_and_reason_clear_stale_values);
    RUN_TEST(test_json_zero_anchor_clears_a_stale_timestamp);
    RUN_TEST(test_http_shaped_payload_freezes_and_unfreezes);
    RUN_TEST(test_mqtt_shaped_payload_without_freeze_keeps_the_freeze);
    return UNITY_END();
}
