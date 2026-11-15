#include "activityLog.h"

#include <Arduino.h>
#include <esp_log.h>

#include "constants/LogTags.h"
#include "machine.h"
#include "magnetics.h"
#include "mqtt.h"
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
                    // A client stopped for the Wi-Fi setup portal starts
                    // again here once the portal has closed. Connected, the
                    // box reads the dashboard's state once for whatever was
                    // published while it could not listen.
                    if (serviceMqttPause()) {
                        noteMqttReconcile(lkbx->confirmServerClear());
                    }
                }
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
        },
        Tasks::activityLog.name, Tasks::activityLog.stackSize, nullptr,
        Tasks::activityLog.priority, &TaskHandles::activityLog,
        Tasks::activityLog.core);
}
