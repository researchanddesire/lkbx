#ifndef LOCKBOX_MEMORY_H
#define LOCKBOX_MEMORY_H

#include <Esp.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "constants/LogTags.h"
#include "esp_log.h"
#include "tasks/TaskParams.h"

inline void printMemoryDiagnostics(const char* src) {
    ESP_LOGD(MEMORY_TAG, "ESP32 Memory Diagnostics: %s", src);
    ESP_LOGD(MEMORY_TAG, "-------------------------");
    ESP_LOGD(MEMORY_TAG, "Total heap size: %d bytes", ESP.getHeapSize());
    ESP_LOGD(MEMORY_TAG, "Free heap: %d bytes", ESP.getFreeHeap());
    ESP_LOGD(MEMORY_TAG, "Minimum free heap ever: %d bytes", ESP.getMinFreeHeap());
    ESP_LOGD(MEMORY_TAG, "Max allocatable block: %d bytes", ESP.getMaxAllocHeap());
    ESP_LOGD(MEMORY_TAG, "Total PSRAM: %d bytes", ESP.getPsramSize());
    ESP_LOGD(MEMORY_TAG, "Free PSRAM: %d bytes", ESP.getFreePsram());

    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_INTERNAL);
    ESP_LOGD(MEMORY_TAG, "Internal RAM:");
    ESP_LOGD(MEMORY_TAG, "Total size: %d bytes", info.total_free_bytes + info.total_allocated_bytes);
    ESP_LOGD(MEMORY_TAG, "Free: %d bytes", info.total_free_bytes);
    ESP_LOGD(MEMORY_TAG, "Allocated: %d bytes", info.total_allocated_bytes);
    ESP_LOGD(MEMORY_TAG, "Minimum free ever: %d bytes", info.minimum_free_bytes);

    uint32_t flashChipSize = spi_flash_get_chip_size();
    ESP_LOGD(MEMORY_TAG, "Flash Chip Information:");
    ESP_LOGD(MEMORY_TAG, "Flash Chip Size: %d bytes", flashChipSize);

    ESP_LOGD(MEMORY_TAG, "-------------------------");
}

inline void printMemoryDiagnostics(String src) {
    printMemoryDiagnostics(src.c_str());
}

#ifdef MEMORY_LOGGING
struct TaskWatermarkEntry {
    const char* name;
    TaskHandle_t* handle;
};

static const TaskWatermarkEntry watermarkTasks[] = {
    {"wifi",           &TaskHandles::wifi},
    {"button",         &TaskHandles::button},
    {"idle",           &TaskHandles::idle},
    {"activityLog",    &TaskHandles::activityLog},
    {"nimble",         &TaskHandles::nimble},
    {"httpWorker",     &TaskHandles::httpWorker},
    {"hourlyWeb",      &TaskHandles::hourlyWeb},
    {"mqttHello",      &TaskHandles::mqttHello},
    {"headerBar",      &TaskHandles::headerBar},
    {"currentWatch",   &TaskHandles::currentWatch},
    {"drawHello",      &TaskHandles::drawHello},
    {"drawMenu",       &TaskHandles::drawMenu},
    {"unlockTimer",    &TaskHandles::unlockTimer},
    {"drawLockAnim",   &TaskHandles::drawLockAnim},
    {"loader",         &TaskHandles::loader},
    {"breakTimer",     &TaskHandles::breakTimer},
    {"preflightScreen",&TaskHandles::preflightScreen},
    {"timerConfig",    &TaskHandles::timerConfig},
    {"delayDispatch",  &TaskHandles::delayDispatch},
    {"checkWifi",      &TaskHandles::checkWifi},
    {"calibrate",      &TaskHandles::calibrate},
    {"checkUpdates",   &TaskHandles::checkUpdates},
    {"pairingCheck",   &TaskHandles::pairingCheck},
    {"pairingPoll",    &TaskHandles::pairingPoll},
    {"syncLockState",  &TaskHandles::syncLockState},
    {"wifiConnect",    &TaskHandles::wifiConnect},
    {"mqttConnect",    &TaskHandles::mqttConnect},
    {"update",         &TaskHandles::update},
};

static void stackMonitorTask(void*) {
    constexpr size_t numTasks =
        sizeof(watermarkTasks) / sizeof(watermarkTasks[0]);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        ESP_LOGI(MEMORY_TAG, "=== Stack High Water Marks (bytes free) ===");
        for (size_t i = 0; i < numTasks; i++) {
            TaskHandle_t h = *(watermarkTasks[i].handle);
            if (h == nullptr) continue;
            UBaseType_t hwm = uxTaskGetStackHighWaterMark(h);
            ESP_LOGI(MEMORY_TAG, "  %-18s %5u bytes free", watermarkTasks[i].name,
                     (unsigned)(hwm));
        }

        ESP_LOGI(MEMORY_TAG, "  %-18s %5u bytes free", "stackMonitor",
                 (unsigned)uxTaskGetStackHighWaterMark(nullptr));

        printMemoryDiagnostics("stackMonitor");
    }
}

static void initStackMonitor() {
    xTaskCreatePinnedToCore(stackMonitorTask, Tasks::taskMonitor.name,
                            Tasks::taskMonitor.stackSize, nullptr,
                            Tasks::taskMonitor.priority,
                            &TaskHandles::taskMonitor,
                            Tasks::taskMonitor.core);
}
#endif  // MEMORY_LOGGING

#endif  // LOCKBOX_MEMORY_H
