#include "idle.h"

#include <Arduino.h>
#include <esp_log.h>

#include "constants/LogTags.h"
#include "display.h"
#include "ina.h"
#include "lastInteraction.h"
#include "motor/Motor.h"
#include "sleep.h"
#include "tasks/TaskParams.h"
#include "utils/idleTarget.h"
#include "utils/savedTimerSleep.h"

void initIdleTask() {
    ESP_LOGD(TASKDEBUG_TAG, "Creating idle task");
    xTaskCreatePinnedToCore(
        [](void *pvParameters) {
            IdleState lastIdleState = IdleState::NOT_IDLE;

            bool isCharging = true;
            bool wasPowered = sleepPowerConnected();
            while (true) {
                const bool powered = sleepPowerConnected();
                if (powered && !wasPowered) setNotIdle("USB connected");
                wasPowered = powered;
                auto delta = millis() - lastInteraction;
                isCharging = sleepPowerConnected();
                IdleState targetState = idleTargetFromDelta(delta, isCharging);

                // setNotIdle() may run on another task between the delta read
                // above and idleState assignment. Re-read lastInteraction before
                // clobbering idleState so a wake is not overwritten with a stale
                // PSEUDO_SLEEP / IDLE target.
                if (idleState != targetState) {
                    delta = millis() - lastInteraction;
                    isCharging = sleepPowerConnected();
                    targetState = idleTargetFromDelta(delta, isCharging);

                    if (idleState != targetState) {
                        idleState = targetState;
                        switch (targetState) {
                            case IdleState::NOT_IDLE:
                                ESP_LOGD(IDLE_TAG,
                                         "Setting idle state to NOT_IDLE");
                                break;
                            case IdleState::IDLE:
                                ESP_LOGD(IDLE_TAG, "Setting idle state to IDLE");
                                break;
                            case IdleState::PSEUDO_SLEEP:
                                ESP_LOGD(IDLE_TAG,
                                         "Setting idle state to PSEUDO_SLEEP");
                                break;
                            case IdleState::SLEEP:
                                break;
                        }
                        vTaskDelay(100 / portTICK_PERIOD_MS);
                        continue;
                    }
                }

                if (lastIdleState == idleState &&
                    idleState == IdleState::NOT_IDLE) {
                    vTaskDelay(1000 / portTICK_PERIOD_MS);
                    continue;
                } else if (lastIdleState == idleState &&
                           idleState != IdleState::NOT_IDLE) {
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                    continue;
                }

                const uint64_t wakeDelayMs = idleState == IdleState::SLEEP
                    ? savedTimerSleepDelayMs() : 0;
                if (idleState == IdleState::SLEEP) {
                    enterSleep(wakeDelayMs);
                    // A power/serial wake can cancel sleep. Let the next pass
                    // apply the resulting awake or pseudo-sleep peripherals.
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                } else if (idleState == IdleState::PSEUDO_SLEEP) {
                    ledcWrite(0, 0);
                    Motor::clear();
                    ina.powerSave(true);
                } else if (idleState == IdleState::IDLE) {
                    ledcWrite(0, 24);
                    Motor::clear();
                } else {
                    ledcWrite(0, 255);
                    ina.powerSave(false);
                }

                lastIdleState = idleState;
                vTaskDelay(100 / portTICK_PERIOD_MS);
            }
        },
        Tasks::idle.name, Tasks::idle.stackSize, nullptr, Tasks::idle.priority,
        &TaskHandles::idle, Tasks::idle.core);
}
