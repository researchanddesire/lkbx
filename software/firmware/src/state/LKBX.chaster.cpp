#include "LKBX.h"

#include <WiFi.h>
#include <esp_heap_caps.h>

#include "constants/dashboard.h"
#include "services/deviceIdentity.h"
#include "services/http_worker.h"
#include "utils/chasterAsk.h"
#include "utils/getEfuseMac.h"

// A Chaster session asks the dashboard before it opens for a break or unlocks
// at the end of its timer. The rules, the wire format and the offline
// behaviour are in utils/chasterAsk.h; the pages and transitions are in
// ChasterAskTransitions.inc.
//
// The question runs on the delay task (LKBX::delayTask), in steps that never
// block it: hand the request to the HTTP worker, look for the answer on each
// pass, then run the transition the answer leads to. No task is created, so
// the box can always ask, however little internal RAM is free.

namespace {

    // The question the delay task is working on. Only that task touches it.
    struct AskInFlight {
        // Which question (LKBX::askOrder), or kNoAskOrder before the first.
        uint32_t order = kNoAskOrder;
        AskAction action = AskAction::UNLOCK;
        String sessionId;
        // The request at the HTTP worker, until its answer has been taken.
        HttpPending *pending = nullptr;
        // The request has still to be handed to the HTTP worker.
        bool toSend = false;
        uint32_t sendAtMs = 0;
        uint8_t sendAttempts = 0;
    };
    AskInFlight ask;

    String heldSessionId() {
        String sessionId;
        if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) ==
            pdTRUE) {
            preferences.begin(Keys::session);
            sessionId = preferences.getString(Keys::Session::sessionId, "");
            preferences.end();
            xSemaphoreGive(preferencesMutex);
        }
        return sessionId;
    }

    // Hands one question to the HTTP worker. Null when the worker could not
    // take it.
    HttpPending *sendAsk(AskAction action, const String &sessionId) {
        JsonDocument request;
        String url = String(RAD_SERVER) + Str::Api::LOCKBOX_ASK;
        const String token =
            signedInWithV2() ? deviceTokenForRequest() : String();
        if (token.length() > 0) {
            request[Str::Json::DEVICE_TOKEN] = token;
            url = String(RAD_SERVER) + Str::Api::LOCKBOX_V2_ASK;
        } else {
            request[Str::Json::MAC_ADDRESS] = getMacAddresss();
            const String deviceSecret = deviceSecretForRequest();
            if (deviceSecret.length() > 0) {
                request[Str::Json::DEVICE_SECRET] = deviceSecret;
            }
        }
        request[chasterAsk::kSessionId] = sessionId;
        request[chasterAsk::kAction] = askActionText(action);

        return postDetached(url, request, chasterAsk::kTimeoutMs);
    }

    // One question, one answer. Anything that is not a complete answer to
    // this question comes back as UNANSWERED, which keeps the box locked.
    AskAnswer readAnswer(int status, const String &payload, AskAction action,
                         const String &sessionId) {
        JsonDocument response;
        if (status != 200 || deserializeJson(response, payload)) {
            ESP_LOGW(CHASTER_ASK_TAG, "No answer to the %s question (HTTP %d)",
                     askActionText(action), status);
            return AskAnswer{};
        }
        return classifyAskAnswer(status, response, sessionId.c_str(), action);
    }

}  // namespace

// The transition into "locked.asking" asks. The delay task picks the question
// up on its next pass, within 100 ms.
void LKBX::startChasterAsk() {
    askReason[0] = '\0';
    askOutcome = AskOutcome::UNANSWERED;
    // Only transitions write the order, one at a time.
    askOrder.store(nextAskOrder(askOrder.load(), askAction));
}

// One pass of the delay task. Never waits for the network.
void LKBX::serviceChasterAsk() {
    const uint32_t newest = askOrder.load();

    if (ask.order != newest) {
        // A newer question replaced the one in flight: its answer is for
        // nobody.
        if (ask.pending != nullptr) abandonHttp(ask.pending);
        ask.order = newest;
        ask.action = askOrderAction(newest);
        ask.sessionId = heldSessionId();
        ask.toSend = true;
        ask.sendAtMs = millis();
        ask.sendAttempts = 0;
    }

    if (ask.toSend) {
        if (static_cast<int32_t>(millis() - ask.sendAtMs) < 0) return;
        ask.toSend = false;

        // Nobody to ask about, or no way to ask: that is no answer.
        if (ask.sessionId.isEmpty() || WiFiClass::status() != WL_CONNECTED) {
            answerChasterAsk(AskAnswer{});
            return;
        }

        ++ask.sendAttempts;
        ask.pending = sendAsk(ask.action, ask.sessionId);
        if (ask.pending != nullptr) return;

        ESP_LOGW(CHASTER_ASK_TAG,
                 "Could not send the %s question (attempt %u of %u): largest "
                 "free internal block %u bytes, %u bytes free",
                 askActionText(ask.action),
                 static_cast<unsigned>(ask.sendAttempts),
                 static_cast<unsigned>(chasterAsk::kSendAttempts),
                 static_cast<unsigned>(
                     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        if (askSendMayRetry(ask.sendAttempts)) {
            ask.toSend = true;
            ask.sendAtMs = millis() + chasterAsk::kSendRetryMs;
            return;
        }
        // Report it at once, as the break preflight reports its own result,
        // so the box stays locked on a page that says so instead of waiting
        // on the asking page forever.
        ESP_LOGE(CHASTER_ASK_TAG, "Could not start the Chaster question");
        answerChasterAsk(AskAnswer{});
        return;
    }

    // Nothing asked yet, or the newest question already has its answer.
    if (ask.pending == nullptr) return;

    int status = -1;
    String payload;
    if (!takeHttpResult(ask.pending, status, &payload)) return;
    answerChasterAsk(readAnswer(status, payload, ask.action, ask.sessionId));
}

// Runs what the answer to the question in flight leads to. On the delay task.
void LKBX::answerChasterAsk(const AskAnswer &answer) {
    const AskAction action = ask.action;

    // A newer question replaced this one, or the box has moved to another
    // session while it waited: this answer is for neither.
    if (ask.order != askOrder.load() || ask.sessionId != heldSessionId()) {
        ESP_LOGW(CHASTER_ASK_TAG, "Dropping a late answer to the %s question",
                 askActionText(action));
        return;
    }

    askOutcome = answer.outcome;
    copyBoundedString(askReason, sizeof(askReason), answer.reason);

    switch (answer.outcome) {
        case AskOutcome::GRANTED:
            ESP_LOGI(CHASTER_ASK_TAG, "Chaster allows the %s",
                     askActionText(action));
            if (action == AskAction::BREAK) {
                // A granted break is the dashboard starting the break: it
                // opens through the same event, from whatever locked page is
                // up.
                sm->process_event(Break{});
                break;
            }
            // The deadline, a freeze and a pending offer are checked again
            // before the motor moves. acceptNaturalUnlock records the session
            // as completed, so it runs only for a question still waiting.
            switch (unlockGrantStep(sm->is("locked.asking"_s),
                                    sm->is("locked.asking"_s) &&
                                        acceptNaturalUnlock())) {
                case UnlockGrant::UNLOCK:
                    sm->process_event(Unlock{});
                    break;
                case UnlockGrant::RETURN:
                    sm->process_event(Error{.error = ErrorT::ASK_RETURN});
                    break;
                case UnlockGrant::DROP:
                default:
                    ESP_LOGW(CHASTER_ASK_TAG,
                             "Nobody is waiting for the unlock answer");
                    break;
            }
            break;
        case AskOutcome::REFUSED:
            ESP_LOGI(CHASTER_ASK_TAG, "Chaster refuses the %s: %s",
                     askActionText(action), answer.reason);
            sm->process_event(Error{.error = ErrorT::ASK_ANSWERED});
            break;
        case AskOutcome::STALE_SESSION:
            // The dashboard holds no such session. Its own state says what
            // the box should hold instead, including nothing at all.
            ESP_LOGW(CHASTER_ASK_TAG,
                     "The dashboard does not know this session -- reading its "
                     "state");
            sm->process_event(Error{.error = ErrorT::ASK_ANSWERED});
            confirmServerClear();
            break;
        case AskOutcome::UNANSWERED:
        default:
            ESP_LOGW(CHASTER_ASK_TAG,
                     "The %s question got no answer; staying locked",
                     askActionText(action));
            sm->process_event(Error{.error = ErrorT::ASK_ANSWERED});
            break;
    }

    // This stack is shared with the Done dispatches. Its low-water mark goes
    // to the log, where a rig run can read it without a special build.
    ESP_LOGI(CHASTER_ASK_TAG,
             "Answered on the delay task: at least %u bytes of its stack "
             "have stayed free",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}
