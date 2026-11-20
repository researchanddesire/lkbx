#include <Arduino.h>
#include <ArduinoJson.h>
#include <unity.h>

#include <string>

#include "../../src/state/Events.h"
#include "../../src/utils/chasterAsk.h"
#include "../../src/utils/syncEffect.h"

using namespace sml;

// Hardware boundaries only are stubbed. The transition rows below are the
// exact fragments used by LKBX::InnerStateMachine, not a copy of their
// behaviour.
namespace Str { namespace Protocol { constexpr const char* BREAK = "break"; } }

struct Context {
    bool chaster = true;
    // The box's own timer is complete and nothing holds it.
    bool ready = true;
    // What the transition into "locked.asking" said the box is asking.
    AskAction asked = AskAction::UNLOCK;
    int unlocks = 0;        // the motor opened to end the session
    int paddleOpens = 0;    // the motor opened for a break or an offer
    int breakTimers = 0;
    int reports = 0;
    int asks = 0;
    int askingPages = 0;
    int answerPages = 0;
};

struct Fixture {
    auto operator()() const {
        auto isChasterSession = [](Context& c) { return c.chaster; };
        auto acceptNaturalUnlock = [](Context& c) { return c.ready; };
        auto isError = [](ErrorT expected) {
            return [expected](const auto& e) { return e.error == expected; };
        };
        auto clearLocalPlay = [](Context&) {};
        auto clearActiveBreak = [](Context&) {};
        auto unlockAction = [](Context& c) { ++c.unlocks; };
        auto movePaddleToUnlock = [](Context& c) { ++c.paddleOpens; };
        auto setAskUnlock = [](Context& c) { c.asked = AskAction::UNLOCK; };
        auto setAskBreak = [](Context& c) { c.asked = AskAction::BREAK; };
        auto drawAskingChaster = [](Context& c) { ++c.askingPages; };
        auto startChasterAsk = [](Context& c) { ++c.asks; };
        auto drawAskAnswer = [](Context& c) { ++c.answerPages; };
        auto startBreakTimer = [](Context& c) { ++c.breakTimers; };
        auto clearLockedReport = [](Context&) {};
        auto sendMessage = [](const char*) {
            return [](Context& c) { ++c.reports; };
        };
        return make_transition_table(
            *"fixture"_s + done = "fixture"_s,
#include "../../src/state/ChasterAskTransitions.inc"
#include "../../src/state/BreakEntryTransitions.inc"
            "locking"_s + done = "home"_s
        );
    }
};
using Machine = sml::sm<Fixture, sml::testing>;

void setUp() {}
void tearDown() {}

namespace {
const Error kAnswered{.error = ErrorT::ASK_ANSWERED};
const Error kReturn{.error = ErrorT::ASK_RETURN};
const Error kAskChaster{.error = ErrorT::ASK_CHASTER};
}  // namespace

// --- unlocking at the end of the timer ---------------------------------------

void test_chaster_session_asks_instead_of_unlocking() {
    Context c;
    c.asked = AskAction::BREAK;
    Machine machine{c};
    machine.set_current_states("locked.ready"_s);
    TEST_ASSERT_TRUE(machine.process_event(ButtonPress{}));
    TEST_ASSERT_TRUE(machine.is("locked.asking"_s));
    TEST_ASSERT_TRUE(c.asked == AskAction::UNLOCK);
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
    TEST_ASSERT_EQUAL_INT(1, c.asks);
    TEST_ASSERT_EQUAL_INT(1, c.askingPages);
}

void test_other_sessions_unlock_as_before() {
    Context c;
    c.chaster = false;
    Machine machine{c};
    machine.set_current_states("locked.ready"_s);
    TEST_ASSERT_TRUE(machine.process_event(ButtonPress{}));
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.asks);
}

// The asking task reports a granted unlock as Unlock, after it has checked
// the session again (unlockGrantStep).
void test_granted_unlock_opens_once() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.asking"_s);
    TEST_ASSERT_TRUE(machine.process_event(Unlock{}));
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
}

void test_granted_unlock_rechecks_the_session() {
    // Nobody is waiting any more: the answer is dropped.
    TEST_ASSERT_TRUE(unlockGrantStep(false, true) == UnlockGrant::DROP);
    TEST_ASSERT_TRUE(unlockGrantStep(false, false) == UnlockGrant::DROP);
    // Time was added, or a freeze landed, while the box was asking.
    TEST_ASSERT_TRUE(unlockGrantStep(true, false) == UnlockGrant::RETURN);
    TEST_ASSERT_TRUE(unlockGrantStep(true, true) == UnlockGrant::UNLOCK);

    Context c;
    Machine machine{c};
    machine.set_current_states("locked.asking"_s);
    TEST_ASSERT_TRUE(machine.process_event(kReturn));
    TEST_ASSERT_TRUE(machine.is("locked.return"_s));
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.answerPages);
}

// Refused and unanswered share one page: the box stays locked either way.
// Offline, a Chaster session never opens on its own timer.
void test_refused_or_unanswered_unlock_never_opens() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.asking"_s);
    TEST_ASSERT_TRUE(machine.process_event(kAnswered));
    TEST_ASSERT_TRUE(machine.is("locked.askAnswer"_s));
    TEST_ASSERT_EQUAL_INT(1, c.answerPages);
    // The same answer again finds nobody asking.
    TEST_ASSERT_FALSE(machine.process_event(kAnswered));
    TEST_ASSERT_FALSE(machine.process_event(kReturn));
    TEST_ASSERT_TRUE(machine.is("locked.askAnswer"_s));
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.paddleOpens);
}

// The lock menu, and with it emergency unlock and Wi-Fi settings, is one press
// away from the answer page with either button.
void test_answer_page_leads_to_the_lock_menu() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.askAnswer"_s);
    TEST_ASSERT_TRUE(machine.process_event(ButtonPress{}));
    TEST_ASSERT_TRUE(machine.is("lockMenu"_s));
    machine.set_current_states("locked.askAnswer"_s);
    TEST_ASSERT_TRUE(machine.process_event(BackPress{}));
    TEST_ASSERT_TRUE(machine.is("lockMenu"_s));
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.asks);
}

void test_buttons_do_nothing_while_the_box_is_asking() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.asking"_s);
    TEST_ASSERT_FALSE(machine.process_event(ButtonPress{}));
    TEST_ASSERT_FALSE(machine.process_event(BackPress{}));
    TEST_ASSERT_TRUE(machine.is("locked.asking"_s));
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.asks);
}

void test_an_answer_with_no_question_waiting_is_ignored() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.ready"_s);
    TEST_ASSERT_FALSE(machine.process_event(kAnswered));
    TEST_ASSERT_FALSE(machine.process_event(kReturn));
    TEST_ASSERT_FALSE(machine.process_event(kAskChaster));
    TEST_ASSERT_TRUE(machine.is("locked.ready"_s));
    machine.set_current_states("lockMenu"_s);
    TEST_ASSERT_FALSE(machine.process_event(kAnswered));
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
    TEST_ASSERT_EQUAL_INT(0, c.answerPages);
}

// --- breaks from the wearer's menu ------------------------------------------

void test_break_asks_before_the_box_opens() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.breakPreflight"_s);
    TEST_ASSERT_TRUE(machine.process_event(kAskChaster));
    TEST_ASSERT_TRUE(machine.is("locked.asking"_s));
    TEST_ASSERT_TRUE(c.asked == AskAction::BREAK);
    TEST_ASSERT_EQUAL_INT(1, c.asks);
    TEST_ASSERT_EQUAL_INT(1, c.askingPages);
    TEST_ASSERT_EQUAL_INT(0, c.paddleOpens);
    TEST_ASSERT_EQUAL_INT(0, c.reports);
}

void test_granted_break_opens_and_reports_once() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.asking"_s);
    // The asking task reports a granted break as the break itself.
    TEST_ASSERT_TRUE(machine.process_event(Break{}));
    TEST_ASSERT_TRUE(machine.is("locked.break"_s));
    TEST_ASSERT_EQUAL_INT(1, c.paddleOpens);
    TEST_ASSERT_EQUAL_INT(1, c.breakTimers);
    TEST_ASSERT_EQUAL_INT(1, c.reports);
    // The dashboard's own push of the same break changes nothing.
    TEST_ASSERT_FALSE(machine.process_event(Break{}));
    TEST_ASSERT_EQUAL_INT(1, c.paddleOpens);
    TEST_ASSERT_EQUAL_INT(1, c.reports);
}

void test_refused_or_unanswered_break_never_opens() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.breakPreflight"_s);
    TEST_ASSERT_TRUE(machine.process_event(kAskChaster));
    TEST_ASSERT_TRUE(machine.process_event(kAnswered));
    TEST_ASSERT_TRUE(machine.is("locked.askAnswer"_s));
    TEST_ASSERT_EQUAL_INT(1, c.answerPages);
    TEST_ASSERT_TRUE(machine.process_event(ButtonPress{}));
    TEST_ASSERT_TRUE(machine.is("lockMenu"_s));
    TEST_ASSERT_EQUAL_INT(0, c.paddleOpens);
    TEST_ASSERT_EQUAL_INT(0, c.breakTimers);
    TEST_ASSERT_EQUAL_INT(0, c.reports);
}

// --- a break the dashboard starts --------------------------------------------

template <class State> void assertDashboardBreakOpensFrom(State state) {
    Context c;
    Machine machine{c};
    machine.set_current_states(state);
    TEST_ASSERT_TRUE(machine.process_event(Break{}));
    TEST_ASSERT_TRUE(machine.is("locked.break"_s));
    TEST_ASSERT_EQUAL_INT(1, c.paddleOpens);
    TEST_ASSERT_EQUAL_INT(1, c.breakTimers);
    TEST_ASSERT_EQUAL_INT(1, c.reports);
    TEST_ASSERT_EQUAL_INT(0, c.unlocks);
}

void test_dashboard_break_opens_from_every_locked_page() {
    // The pages BreakEntryTransitions.inc already covered.
    assertDashboardBreakOpensFrom("locked"_s);
    assertDashboardBreakOpensFrom("locked.timer"_s);
    assertDashboardBreakOpensFrom("lockMenu"_s);
    assertDashboardBreakOpensFrom("locked.breakPreflight"_s);
    // The pages that ignored it.
    assertDashboardBreakOpensFrom("locked.ready"_s);
    assertDashboardBreakOpensFrom("locked.ready.addTime"_s);
    assertDashboardBreakOpensFrom("locked.breaking"_s);
    assertDashboardBreakOpensFrom("locked.breakError"_s);
    assertDashboardBreakOpensFrom("locked.breakCooldown"_s);
    // The pages added for asking.
    assertDashboardBreakOpensFrom("locked.asking"_s);
    assertDashboardBreakOpensFrom("locked.askAnswer"_s);
}

// --- messages from the dashboard on the new pages ----------------------------

void test_dashboard_clear_still_unlocks_from_the_new_pages() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.askAnswer"_s);
    TEST_ASSERT_TRUE(machine.process_event(Unlock{}));
    TEST_ASSERT_TRUE(machine.is("locking"_s));
    TEST_ASSERT_EQUAL_INT(1, c.unlocks);
}

void test_new_offer_is_shown_from_the_new_pages() {
    Context c;
    Machine machine{c};
    machine.set_current_states("locked.asking"_s);
    TEST_ASSERT_TRUE(machine.process_event(LockOffered{}));
    TEST_ASSERT_TRUE(machine.is("acceptLock"_s));
    machine.set_current_states("locked.askAnswer"_s);
    TEST_ASSERT_TRUE(machine.process_event(LockOffered{}));
    TEST_ASSERT_TRUE(machine.is("acceptLock"_s));
    TEST_ASSERT_EQUAL_INT(2, c.paddleOpens);
}

// --- reading the dashboard's answer ------------------------------------------

namespace {

AskAnswer classify(int status, const char* json, const char* sessionId,
                   AskAction action) {
    JsonDocument body;
    if (json != nullptr) deserializeJson(body, json);
    return classifyAskAnswer(status, body, sessionId, action);
}

constexpr char kSession[] = "8607f5e1-f052-47e7-9910-1d9180fffa0a";

}  // namespace

void test_answer_grants_only_the_asked_question() {
    const AskAnswer granted = classify(
        200,
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"unlock","allowed":true,"code":"ready","reason":""})",
        kSession, AskAction::UNLOCK);
    TEST_ASSERT_TRUE(granted.outcome == AskOutcome::GRANTED);

    // The same answer does not grant a break, or another session's unlock.
    TEST_ASSERT_TRUE(
        classify(200,
                 R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"unlock","allowed":true})",
                 kSession, AskAction::BREAK)
            .outcome == AskOutcome::UNANSWERED);
    TEST_ASSERT_TRUE(
        classify(200,
                 R"({"ok":true,"sessionId":"another-session","action":"unlock","allowed":true})",
                 kSession, AskAction::UNLOCK)
            .outcome == AskOutcome::UNANSWERED);
    // An answer that names no session or no action grants nothing.
    TEST_ASSERT_TRUE(
        classify(200, R"({"ok":true,"action":"unlock","allowed":true})",
                 kSession, AskAction::UNLOCK)
            .outcome == AskOutcome::UNANSWERED);
    TEST_ASSERT_TRUE(
        classify(200,
                 R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","allowed":true})",
                 kSession, AskAction::UNLOCK)
            .outcome == AskOutcome::UNANSWERED);
}

void test_anything_but_a_complete_answer_is_no_answer() {
    const char* granted =
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"break","allowed":true})";
    // No Wi-Fi or a timeout, and every status that is not 200.
    for (int status : {-1, 0, 201, 204, 400, 401, 404, 429, 500, 503}) {
        TEST_ASSERT_TRUE(classify(status, granted, kSession, AskAction::BREAK)
                             .outcome == AskOutcome::UNANSWERED);
    }
    const char* malformed[] = {
        nullptr,
        "",
        "[]",
        "true",
        R"({"allowed":true})",
        R"({"ok":false,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"break","allowed":true})",
        R"({"ok":"true","sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"break","allowed":true})",
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"break","allowed":"true"})",
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"break","allowed":1})",
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"break"})",
        R"({"ok":true,"sessionId":7,"action":"break","allowed":true})",
    };
    for (const char* json : malformed) {
        TEST_ASSERT_TRUE_MESSAGE(
            classify(200, json, kSession, AskAction::BREAK).outcome ==
                AskOutcome::UNANSWERED,
            json ? json : "(no body)");
    }
    // The box holds no session to ask about.
    TEST_ASSERT_TRUE(classify(200, granted, "", AskAction::BREAK).outcome ==
                     AskOutcome::UNANSWERED);
    TEST_ASSERT_TRUE(classify(200, granted, nullptr, AskAction::BREAK).outcome ==
                     AskOutcome::UNANSWERED);
}

void test_refusal_carries_chasters_reason() {
    const AskAnswer refused = classify(
        200,
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"break","allowed":false,"code":"refused","reason":"No hygiene opening is available yet."})",
        kSession, AskAction::BREAK);
    TEST_ASSERT_TRUE(refused.outcome == AskOutcome::REFUSED);
    TEST_ASSERT_EQUAL_STRING("No hygiene opening is available yet.",
                             refused.reason);

    // A refusal without a code or a reason is still a refusal.
    const AskAnswer bare = classify(
        200,
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"unlock","allowed":false})",
        kSession, AskAction::UNLOCK);
    TEST_ASSERT_TRUE(bare.outcome == AskOutcome::REFUSED);
    TEST_ASSERT_EQUAL_STRING("", bare.reason);
}

void test_reason_is_bounded_and_cleaned() {
    // A long reason is cut to what the page can hold.
    std::string json =
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"unlock","allowed":false,"code":"held","reason":")";
    for (int i = 0; i < 40; i++) json += "Tasks pending ";
    json += R"("})";
    const AskAnswer refused =
        classify(200, json.c_str(), kSession, AskAction::UNLOCK);
    TEST_ASSERT_TRUE(refused.outcome == AskOutcome::REFUSED);
    TEST_ASSERT_TRUE(strlen(refused.reason) < kAskReasonBytes);
    TEST_ASSERT_EQUAL_INT(0, strncmp(refused.reason, "Tasks pending ", 14));

    // Control characters never reach the screen.
    const AskAnswer cleaned = classify(
        200,
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"unlock","allowed":false,"code":"held","reason":"Tasks\u0007 pending\n"})",
        kSession, AskAction::UNLOCK);
    TEST_ASSERT_EQUAL_STRING("Tasks pending", cleaned.reason);
}

void test_chaster_unreachable_is_no_answer_not_a_refusal() {
    const AskAnswer unavailable = classify(
        200,
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"unlock","allowed":false,"code":"unavailable","reason":"Chaster could not be reached."})",
        kSession, AskAction::UNLOCK);
    TEST_ASSERT_TRUE(unavailable.outcome == AskOutcome::UNANSWERED);
}

void test_unknown_session_rereads_the_state() {
    const AskAnswer stale = classify(
        200,
        R"({"ok":true,"sessionId":"8607f5e1-f052-47e7-9910-1d9180fffa0a","action":"unlock","allowed":false,"code":"no_session"})",
        kSession, AskAction::UNLOCK);
    TEST_ASSERT_TRUE(stale.outcome == AskOutcome::STALE_SESSION);
}

// --- which sessions ask ------------------------------------------------------

void test_chaster_flag_follows_the_dashboard() {
    // The payload says so.
    TEST_ASSERT_TRUE(mergeChasterControlled(true, false, true, true));
    TEST_ASSERT_TRUE(mergeChasterControlled(false, false, true, true));
    TEST_ASSERT_FALSE(mergeChasterControlled(false, true, true, false));
    // A payload without the field keeps what the box knows about its session.
    TEST_ASSERT_TRUE(mergeChasterControlled(false, true, false, false));
    TEST_ASSERT_FALSE(mergeChasterControlled(false, false, false, false));
    // A new session never inherits the previous one's flag.
    TEST_ASSERT_FALSE(mergeChasterControlled(true, true, false, false));
}

void test_chaster_flag_is_read_only_from_a_boolean() {
    JsonDocument doc;
    bool value = false;

    deserializeJson(doc, R"({"chasterControlled":true})");
    TEST_ASSERT_TRUE(sessionFlagFromJson(doc, "chasterControlled", value));
    TEST_ASSERT_TRUE(value);

    deserializeJson(doc, R"({"chasterControlled":false})");
    TEST_ASSERT_TRUE(sessionFlagFromJson(doc, "chasterControlled", value));
    TEST_ASSERT_FALSE(value);

    // Absent, null or malformed: the stored value is left alone.
    value = true;
    for (const char* json :
         {R"({})", R"({"chasterControlled":null})",
          R"({"chasterControlled":"false"})", R"({"chasterControlled":0})"}) {
        deserializeJson(doc, json);
        TEST_ASSERT_FALSE_MESSAGE(
            sessionFlagFromJson(doc, "chasterControlled", value), json);
        TEST_ASSERT_TRUE(value);
    }
}

void test_either_ask_field_makes_the_box_ask() {
    // Neither field: the payload says nothing, the stored value stays.
    TEST_ASSERT_FALSE(combineAskFlags(false, false, false, false).present);
    // chasterControlled alone.
    TEST_ASSERT_TRUE(combineAskFlags(true, true, false, false).present);
    TEST_ASSERT_TRUE(combineAskFlags(true, true, false, false).asks);
    TEST_ASSERT_FALSE(combineAskFlags(true, false, false, false).asks);
    // requiresPermission alone, or true next to a false chasterControlled.
    TEST_ASSERT_TRUE(combineAskFlags(false, false, true, true).present);
    TEST_ASSERT_TRUE(combineAskFlags(false, false, true, true).asks);
    TEST_ASSERT_TRUE(combineAskFlags(true, false, true, true).asks);
    // Both present and false: the box stops asking.
    TEST_ASSERT_TRUE(combineAskFlags(true, false, true, false).present);
    TEST_ASSERT_FALSE(combineAskFlags(true, false, true, false).asks);
    // A value without its field counts for nothing.
    TEST_ASSERT_FALSE(combineAskFlags(false, true, false, true).asks);
}

// --- the held-session page ---------------------------------------------------

void test_unlock_hold_is_told_apart_from_a_freeze() {
    TEST_ASSERT_TRUE(isUnlockHoldReason("Waiting for Chaster"));
    TEST_ASSERT_TRUE(isUnlockHoldReason("Waiting for Chaster: Tasks pending"));
    TEST_ASSERT_FALSE(isUnlockHoldReason("Frozen by your keyholder"));
    TEST_ASSERT_FALSE(isUnlockHoldReason(""));
    TEST_ASSERT_FALSE(isUnlockHoldReason(nullptr));

    TEST_ASSERT_EQUAL_STRING(
        "Tasks pending", unlockHoldDetail("Waiting for Chaster: Tasks pending"));
    TEST_ASSERT_EQUAL_STRING("", unlockHoldDetail("Waiting for Chaster"));
    TEST_ASSERT_NULL(unlockHoldDetail("Frozen by your keyholder"));

    // The reason line drops the label the heading already shows.
    TEST_ASSERT_EQUAL_STRING(
        "Tasks pending", heldReasonLine("Waiting for Chaster: Tasks pending"));
    TEST_ASSERT_EQUAL_STRING("Frozen by your keyholder",
                             heldReasonLine("Frozen by your keyholder"));
}

// --- a hidden timer gives nothing away ---------------------------------------

constexpr uint64_t kEnd = 1789188000000ULL;

void test_hidden_timer_does_not_show_an_unlock_hold_before_the_end() {
    // The dashboard publishes the hold up to three minutes before the end.
    for (uint64_t before : {180000ULL, 120000ULL, 30000ULL, 1000ULL, 1ULL}) {
        TEST_ASSERT_FALSE(
            holdShownOnScreen(true, true, false, true, kEnd, kEnd - before));
    }
    // From the deadline on, the box may say it is waiting for Chaster.
    TEST_ASSERT_TRUE(holdShownOnScreen(true, true, false, true, kEnd, kEnd));
    TEST_ASSERT_TRUE(
        holdShownOnScreen(true, true, false, true, kEnd, kEnd + 60000));
}

void test_visible_timer_shows_the_hold_at_once() {
    TEST_ASSERT_TRUE(
        holdShownOnScreen(true, true, true, true, kEnd, kEnd - 180000));
}

void test_real_freeze_is_always_shown() {
    // A freeze does not depend on the time left, hidden timer or not.
    TEST_ASSERT_TRUE(
        holdShownOnScreen(true, false, false, true, kEnd, kEnd - 180000));
    TEST_ASSERT_TRUE(
        holdShownOnScreen(true, false, false, false, kEnd, kEnd - 180000));
}

void test_hold_display_without_a_usable_clock_or_deadline() {
    // No deadline: nothing to give away.
    TEST_ASSERT_TRUE(holdShownOnScreen(true, true, false, true, 0, kEnd));
    // An unverified clock cannot place the deadline: show nothing new.
    TEST_ASSERT_FALSE(
        holdShownOnScreen(true, true, false, false, kEnd, kEnd + 60000));
    // Nothing holds the session.
    TEST_ASSERT_FALSE(holdShownOnScreen(false, true, true, true, kEnd, kEnd));
}

void test_hidden_timer_never_wakes_the_screen_near_the_end() {
    for (uint64_t remaining : {29000ULL, 10000ULL, 1000ULL, 1ULL}) {
        TEST_ASSERT_TRUE(wakesScreenNearExpiry(true, false, remaining));
        TEST_ASSERT_FALSE(wakesScreenNearExpiry(false, false, remaining));
    }
    // A visible countdown wakes only for its last 30 seconds, and never while
    // it is held.
    TEST_ASSERT_FALSE(wakesScreenNearExpiry(true, false, 30000));
    TEST_ASSERT_FALSE(wakesScreenNearExpiry(true, false, 0));
    TEST_ASSERT_FALSE(wakesScreenNearExpiry(true, true, 10000));
}

void test_hidden_timer_sleep_item_behaves_the_same_near_the_end() {
    TEST_ASSERT_TRUE(sleepReturnsToTimer(true, false, true));
    TEST_ASSERT_FALSE(sleepReturnsToTimer(true, false, false));
    TEST_ASSERT_FALSE(sleepReturnsToTimer(true, true, true));
    TEST_ASSERT_FALSE(sleepReturnsToTimer(false, false, true));
    TEST_ASSERT_FALSE(sleepReturnsToTimer(false, false, false));
}

// The dashboard moves a hidden timer's end shortly before it runs out. That
// sync changes nothing on the page, so it must not light the screen.
void test_hidden_timer_sync_wakes_the_screen_only_for_a_new_page() {
    TEST_ASSERT_FALSE(syncWakesScreen(SyncEffect::TIMER_META, false));
    TEST_ASSERT_FALSE(syncWakesScreen(SyncEffect::NONE, false));
    // A new lock, a clear, an offer and a break all change the page.
    TEST_ASSERT_TRUE(syncWakesScreen(SyncEffect::MOTOR_LOCK, false));
    TEST_ASSERT_TRUE(syncWakesScreen(SyncEffect::MOTOR_UNLOCK, false));
    TEST_ASSERT_TRUE(syncWakesScreen(SyncEffect::OFFER, false));
    TEST_ASSERT_TRUE(syncWakesScreen(SyncEffect::BREAK, false));
    // With a visible timer every sync wakes the screen, as before.
    for (SyncEffect effect :
         {SyncEffect::NONE, SyncEffect::TIMER_META, SyncEffect::MOTOR_LOCK,
          SyncEffect::MOTOR_UNLOCK, SyncEffect::OFFER, SyncEffect::BREAK}) {
        TEST_ASSERT_TRUE(syncWakesScreen(effect, true));
    }
}

// --- asking from the delay task ----------------------------------------------

// The delay task asks. It knows the newest question by its order, which also
// says what is asked.
void test_each_question_gets_a_new_order_that_says_what_it_asks() {
    uint32_t order = kNoAskOrder;
    for (int question = 0; question < 6; ++question) {
        const AskAction action =
            question % 2 == 0 ? AskAction::UNLOCK : AskAction::BREAK;
        const uint32_t next = nextAskOrder(order, action);
        TEST_ASSERT_TRUE(next != kNoAskOrder);
        TEST_ASSERT_TRUE(next != order);
        TEST_ASSERT_TRUE(askOrderAction(next) == action);
        order = next;
    }
    // The same question asked again is a new question: the first one's
    // answer no longer counts.
    const uint32_t first = nextAskOrder(kNoAskOrder, AskAction::UNLOCK);
    TEST_ASSERT_TRUE(nextAskOrder(first, AskAction::UNLOCK) != first);
}

void test_the_order_never_wraps_to_nothing_asked() {
    // The highest question number, for an unlock and for a break.
    const uint32_t lastUnlock = 0xFFFFFFFEu;
    const uint32_t wrapped = nextAskOrder(lastUnlock, AskAction::UNLOCK);
    TEST_ASSERT_TRUE(wrapped != kNoAskOrder);
    TEST_ASSERT_TRUE(wrapped != lastUnlock);
    TEST_ASSERT_TRUE(askOrderAction(wrapped) == AskAction::UNLOCK);
    TEST_ASSERT_TRUE(nextAskOrder(0xFFFFFFFFu, AskAction::BREAK) !=
                     kNoAskOrder);
}

// When the HTTP worker cannot take the question the box tries once more, then
// says it could not ask. It never waits on the asking page forever.
void test_a_question_that_cannot_be_sent_is_tried_once_more() {
    TEST_ASSERT_EQUAL_INT(2, chasterAsk::kSendAttempts);
    TEST_ASSERT_TRUE(askSendMayRetry(1));
    TEST_ASSERT_FALSE(askSendMayRetry(2));
    TEST_ASSERT_FALSE(askSendMayRetry(3));
    TEST_ASSERT_TRUE(chasterAsk::kSendRetryMs > 0);
    TEST_ASSERT_TRUE(chasterAsk::kSendRetryMs <= 2000);
}

// --- a break on a Chaster session --------------------------------------------

void test_chaster_break_does_not_end_at_the_boxs_deadline() {
    // Other sessions keep the existing rule.
    TEST_ASSERT_TRUE(breakEndsAtLockWindow(false, true, kEnd, kEnd + 1));
    TEST_ASSERT_FALSE(breakEndsAtLockWindow(false, true, kEnd, kEnd));
    TEST_ASSERT_FALSE(breakEndsAtLockWindow(false, false, kEnd, kEnd + 1));
    TEST_ASSERT_FALSE(breakEndsAtLockWindow(false, true, 0, kEnd + 1));
    // Chaster, the keyholder or the wearer ends a Chaster session's break.
    TEST_ASSERT_FALSE(breakEndsAtLockWindow(true, true, kEnd, kEnd + 1));
    TEST_ASSERT_FALSE(breakEndsAtLockWindow(true, true, kEnd, kEnd + 86400000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_chaster_session_asks_instead_of_unlocking);
    RUN_TEST(test_other_sessions_unlock_as_before);
    RUN_TEST(test_granted_unlock_opens_once);
    RUN_TEST(test_granted_unlock_rechecks_the_session);
    RUN_TEST(test_refused_or_unanswered_unlock_never_opens);
    RUN_TEST(test_answer_page_leads_to_the_lock_menu);
    RUN_TEST(test_buttons_do_nothing_while_the_box_is_asking);
    RUN_TEST(test_an_answer_with_no_question_waiting_is_ignored);
    RUN_TEST(test_break_asks_before_the_box_opens);
    RUN_TEST(test_granted_break_opens_and_reports_once);
    RUN_TEST(test_refused_or_unanswered_break_never_opens);
    RUN_TEST(test_dashboard_break_opens_from_every_locked_page);
    RUN_TEST(test_dashboard_clear_still_unlocks_from_the_new_pages);
    RUN_TEST(test_new_offer_is_shown_from_the_new_pages);
    RUN_TEST(test_answer_grants_only_the_asked_question);
    RUN_TEST(test_anything_but_a_complete_answer_is_no_answer);
    RUN_TEST(test_refusal_carries_chasters_reason);
    RUN_TEST(test_reason_is_bounded_and_cleaned);
    RUN_TEST(test_chaster_unreachable_is_no_answer_not_a_refusal);
    RUN_TEST(test_unknown_session_rereads_the_state);
    RUN_TEST(test_chaster_flag_follows_the_dashboard);
    RUN_TEST(test_chaster_flag_is_read_only_from_a_boolean);
    RUN_TEST(test_either_ask_field_makes_the_box_ask);
    RUN_TEST(test_unlock_hold_is_told_apart_from_a_freeze);
    RUN_TEST(test_hidden_timer_does_not_show_an_unlock_hold_before_the_end);
    RUN_TEST(test_visible_timer_shows_the_hold_at_once);
    RUN_TEST(test_real_freeze_is_always_shown);
    RUN_TEST(test_hold_display_without_a_usable_clock_or_deadline);
    RUN_TEST(test_hidden_timer_never_wakes_the_screen_near_the_end);
    RUN_TEST(test_hidden_timer_sleep_item_behaves_the_same_near_the_end);
    RUN_TEST(test_hidden_timer_sync_wakes_the_screen_only_for_a_new_page);
    RUN_TEST(test_each_question_gets_a_new_order_that_says_what_it_asks);
    RUN_TEST(test_the_order_never_wraps_to_nothing_asked);
    RUN_TEST(test_a_question_that_cannot_be_sent_is_tried_once_more);
    RUN_TEST(test_chaster_break_does_not_end_at_the_boxs_deadline);
    return UNITY_END();
}
