#include "activityLog.h"

#include <Arduino.h>
#include <esp_log.h>

#include "constants/LogTags.h"
#include "machine.h"
#include "magnetics.h"
#include "tasks/TaskParams.h"

void initActivityLogTask() {
    ESP_LOGD(TASKDEBUG_TAG, "Creating activity log task");
    xTaskCreatePinnedToCore(
        [](void *) {
            while (true) {
                if (lkbx) {
                    updateStablePaddleState();
                    lkbx->checkPendingStateChange();
                    checkPendingSensorChange();
                }
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
        },
        Tasks::activityLog.name, Tasks::activityLog.stackSize, nullptr,
        Tasks::activityLog.priority, &TaskHandles::activityLog,
        Tasks::activityLog.core);
}
