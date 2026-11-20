#include "buttons.h"

#include <Arduino.h>
#include <esp_log.h>

#include "constants/LogTags.h"
#include "constants/Pins.h"
#include "constants/strings.h"
#include "lastInteraction.h"
#include "machine.h"
#include "state/Events.h"
#include "tasks/TaskParams.h"

OneButton *buttonBack = nullptr;
OneButton *buttonEnter = nullptr;
OneButton *buttonPowerDet = nullptr;

static int hardResetRequestTimer = 0;

// #region agent log
static const char *DBG_TAG = "DBG-d52718";

static const char *getStateName() {
    static char buf[64] = "unknown";
    if (lkbx && lkbx->sm) {
        lkbx->sm->visit_current_states(
            [](auto state) { strncpy(buf, state.c_str(), sizeof(buf) - 1); });
    } else {
        strncpy(buf, lkbx ? "sm=null" : "lkbx=null", sizeof(buf) - 1);
    }
    return buf;
}
// #endregion

void initButtons() {
    buttonBack = new OneButton(Pins::Buttons::buttonBack, true);
    buttonEnter = new OneButton(Pins::Buttons::buttonEnter, true);
    buttonPowerDet = new OneButton(Pins::Buttons::buttonPowerDet, true);

    buttonEnter->attachClick([]() {
        const bool wasAsleep = (idleState != IdleState::NOT_IDLE);
        setNotIdle(Str::Input::ENTER_CLICK);
        // #region agent log
        ESP_LOGW(DBG_TAG,
                 "H1/H2: enter click | wasAsleep=%d | idle=%d | state=%s",
                 wasAsleep, (int)idleState, getStateName());
        // #endregion
        if (wasAsleep) {
            return;
        }
        lkbx->sm->process_event(ButtonPress{});
    });
    buttonEnter->attachDoubleClick([]() {
        const bool wasAsleep = (idleState != IdleState::NOT_IDLE);
        setNotIdle(Str::Input::ENTER_DOUBLE);
        if (wasAsleep) {
            return;
        }
        lkbx->sm->process_event(ButtonPress{});
    });
    buttonEnter->attachLongPressStart([]() {
        const bool wasAsleep = (idleState != IdleState::NOT_IDLE);
        setNotIdle(Str::Input::ENTER_LONG);
        if (wasAsleep) {
            return;
        }
        lkbx->sm->process_event(LongPress{});
    });

    buttonBack->attachClick([]() {
        const bool wasAsleep = (idleState != IdleState::NOT_IDLE);
        setNotIdle(Str::Input::BACK_CLICK);
        if (wasAsleep) {
            return;
        }
        lkbx->sm->process_event(BackPress{});
    });
    buttonBack->attachDoubleClick([]() {
        const bool wasAsleep = (idleState != IdleState::NOT_IDLE);
        setNotIdle(Str::Input::BACK_DOUBLE);
        if (wasAsleep) {
            return;
        }
        lkbx->sm->process_event(BackPress{});
    });
    buttonBack->attachLongPressStart([]() {
        const bool wasAsleep = (idleState != IdleState::NOT_IDLE);
        setNotIdle(Str::Input::BACK_LONG);
        if (wasAsleep) {
            return;
        }
        lkbx->sm->process_event(BackLongPress{});
    });

    ESP_LOGD(TASKDEBUG_TAG, "Creating button task");
    const BaseType_t created = xTaskCreatePinnedToCore(
        [](void *pvParameters) {
            while (true) {
                buttonEnter->tick();
                buttonPowerDet->tick();
                buttonBack->tick();

                if (digitalRead(Pins::Buttons::buttonEnter) == LOW) {
                    hardResetRequestTimer += 25;
                    if (hardResetRequestTimer >= 10000) {
                        ESP.restart();
                    }
                } else {
                    hardResetRequestTimer = 0;
                }

                vTaskDelay(25 / portTICK_PERIOD_MS);
            }
        },
        Tasks::button.name, Tasks::button.stackSize, nullptr,
        Tasks::button.priority, &TaskHandles::button, Tasks::button.core);
    if (created != pdPASS) {
        // Without this task no button works: a locked box could not reach its
        // lock menu or Emergency Unlock. This runs in setup(), before TLS and
        // the page tasks take memory, so starting again is the way out.
        ESP_LOGE(TASKDEBUG_TAG, "Could not start the button task; restarting");
        delay(1000);
        ESP.restart();
    }
}
