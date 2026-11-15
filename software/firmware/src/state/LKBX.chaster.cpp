#include "LKBX.h"

#include <WiFi.h>

#include <new>

#include "constants/dashboard.h"
#include "services/deviceIdentity.h"
#include "services/http_worker.h"
#include "utils/chasterAsk.h"
#include "utils/getEfuseMac.h"

// A Chaster session asks the dashboard before it opens for a break or unlocks
// at the end of its timer. The rules, the wire format and the offline
// behaviour are in utils/chasterAsk.h; the pages and transitions are in
// ChasterAskTransitions.inc.

namespace {

    struct ChasterAskRequest {
        LKBX *lkbx;
        AskAction action;
        uint32_t generation;
    };

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

    // One question, one answer. Anything that is not a complete answer to
    // this question comes back as UNANSWERED, which keeps the box locked.
    AskAnswer askDashboard(AskAction action, const String &sessionId) {
        if (sessionId.isEmpty() || WiFiClass::status() != WL_CONNECTED) {
            return AskAnswer{};
        }

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

        String payload;
        const int status = post(url, request, &payload, chasterAsk::kTimeoutMs);
        JsonDocument response;
        if (status != 200 || deserializeJson(response, payload)) {
            ESP_LOGW(CHASTER_ASK_TAG, "No answer to the %s question (HTTP %d)",
                     askActionText(action), status);
            return AskAnswer{};
        }
        return classifyAskAnswer(status, response, sessionId.c_str(), action);
    }

}  // namespace

void LKBX::chasterAskTask(void *pvParameters) {
    auto *request = static_cast<ChasterAskRequest *>(pvParameters);
    LKBX *plkbx = request->lkbx;
    const AskAction action = request->action;
    const uint32_t generation = request->generation;
    delete request;

    const String sessionId = heldSessionId();
    const AskAnswer answer = askDashboard(action, sessionId);

    // A newer question replaced this one, or the box has moved to another
    // session while it waited: this answer is for neither.
    if (generation != plkbx->askGeneration.load() ||
        sessionId != heldSessionId()) {
        ESP_LOGW(CHASTER_ASK_TAG, "Dropping a late answer to the %s question",
                 askActionText(action));
        vTaskDelete(nullptr);
        return;
    }

    copyBoundedString(plkbx->askReason, sizeof(plkbx->askReason),
                      answer.reason);

    switch (answer.outcome) {
        case AskOutcome::GRANTED:
            ESP_LOGI(CHASTER_ASK_TAG, "Chaster allows the %s",
                     askActionText(action));
            // A granted break is the dashboard starting the break: it opens
            // through the same event, from whatever locked page is up.
            if (action == AskAction::BREAK) {
                plkbx->sm->process_event(Break{});
            } else {
                plkbx->sm->process_event(AskGranted{});
            }
            break;
        case AskOutcome::REFUSED:
            ESP_LOGI(CHASTER_ASK_TAG, "Chaster refuses the %s: %s",
                     askActionText(action), answer.reason);
            plkbx->sm->process_event(AskRefused{});
            break;
        case AskOutcome::STALE_SESSION:
            // The dashboard holds no such session. Its own state says what
            // the box should hold instead, including nothing at all.
            ESP_LOGW(CHASTER_ASK_TAG,
                     "The dashboard does not know this session -- reading its "
                     "state");
            plkbx->sm->process_event(AskUnanswered{});
            plkbx->confirmServerClear();
            break;
        case AskOutcome::UNANSWERED:
        default:
            ESP_LOGW(CHASTER_ASK_TAG, "The %s question got no answer; staying "
                                      "locked",
                     askActionText(action));
            plkbx->sm->process_event(AskUnanswered{});
            break;
    }

    vTaskDelete(nullptr);
}

void LKBX::startChasterAsk(AskAction action) {
    askReason[0] = '\0';
    auto *request = new (std::nothrow) ChasterAskRequest{
        .lkbx = this,
        .action = action,
        .generation = askGeneration.fetch_add(1) + 1,
    };
    ESP_LOGD(TASKDEBUG_TAG, "Creating Chaster ask task");
    if (request != nullptr &&
        xTaskCreatePinnedToCore(chasterAskTask, Tasks::chasterAsk.name,
                                Tasks::chasterAsk.stackSize, request,
                                Tasks::chasterAsk.priority, nullptr,
                                Tasks::chasterAsk.core) == pdPASS) {
        return;
    }

    // No memory to ask: that is no answer. Report it at once, as the break
    // preflight reports its own result, so the box stays locked on a page that
    // says so instead of waiting on this one forever.
    delete request;
    ESP_LOGE(CHASTER_ASK_TAG, "Could not start the Chaster question");
    sm->process_event(AskUnanswered{});
}
