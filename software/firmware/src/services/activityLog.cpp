#include "activityLog.h"

#include <Arduino.h>
#include <esp_log.h>

#include "constants/LogTags.h"
#include "machine.h"
#include "magnetics.h"
#include "memoryWatch.h"
#include "mqtt.h"
#include "tasks/TaskParams.h"
#include "wm.h"

void initActivityLogTask() {
    ESP_LOGD(TASKDEBUG_TAG, "Creating activity log task");
    const BaseType_t created = xTaskCreatePinnedToCore(
        [](void *) {
            while (true) {
                if (lkbx) {
                    updateStablePaddleState();
                    lkbx->checkPendingStateChange();
                    checkPendingSensorChange();
                    // The Wi-Fi setup portal is opened here, not by the
                    // page: the MQTT client is stopped first, so the
                    // portal's task never has to fit beside it.
                    serviceWifiPortal();
                    // A client that is not running (stopped for the portal,
                    // or never started at boot) starts again here. Connected,
                    // the box reads the dashboard's state once for whatever
                    // was published while it could not listen.
                    if (serviceMqttPause()) lkbx->confirmServerClear();
                    // A Wi-Fi station that has stayed down is begun again.
                    serviceWifiStation();
                    // A station that is up while nothing gets through is
                    // looked into, and in the end the box restarts itself.
                    serviceNetworkRecovery();
                    // The memory picture, once a minute, for the log.
                    serviceMemoryWatch();
                }
                vTaskDelay(50 / portTICK_PERIOD_MS);
            }
        },
        Tasks::activityLog.name, Tasks::activityLog.stackSize, nullptr,
        Tasks::activityLog.priority, &TaskHandles::activityLog,
        Tasks::activityLog.core);
    if (created == pdPASS) return;
    // This task opens the Wi-Fi setup portal, starts the MQTT client again,
    // restores a missing countdown and repeats unsent reports. It is created
    // in setup(), before TLS and the page tasks take memory, so starting
    // again is the way out, as for the delay task.
    ESP_LOGE(TASKDEBUG_TAG, "Could not start the activity log task; restarting");
    delay(1000);
    esp_restart();
}
