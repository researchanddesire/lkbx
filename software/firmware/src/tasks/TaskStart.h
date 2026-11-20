#ifndef LOCKBOX_TASKSTART_H
#define LOCKBOX_TASKSTART_H

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "TaskParams.h"
#include "constants/LogTags.h"

// A task's stack is one contiguous block of internal RAM. On R8 hardware the
// largest free block is at times smaller than a page task's stack, and then
// xTaskCreate fails: a page never draws, a countdown never runs. Creations on
// UI and lock paths go through here, so a failure is logged with the memory
// picture and the caller can act on it instead of carrying on as if the task
// existed.
inline bool startTaskChecked(TaskFunction_t task, const TaskParams &params,
                             void *argument, TaskHandle_t *handle) {
    if (xTaskCreatePinnedToCore(task, params.name, params.stackSize, argument,
                                params.priority, handle,
                                params.core) == pdPASS) {
        return true;
    }
    ESP_LOGE(TASKDEBUG_TAG,
             "Could not start task %s (%u byte stack): largest free internal "
             "block %u bytes, %u bytes free",
             params.name, static_cast<unsigned>(params.stackSize),
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    return false;
}

// The same, trying again after a short wait. A page's task is usually created
// while the task of the page before it still holds its stack. That task ends
// within a few hundred milliseconds of the state change (it polls the state
// without the state machine's lock), which frees a block of about the size
// this one needs. The caller is blocked for at most (attempts - 1) * waitMs.
inline bool startTaskWithRetry(TaskFunction_t task, const TaskParams &params,
                               void *argument, TaskHandle_t *handle,
                               int attempts = 3, uint32_t waitMs = 150) {
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        if (startTaskChecked(task, params, argument, handle)) return true;
        if (attempt < attempts) vTaskDelay(pdMS_TO_TICKS(waitMs));
    }
    return false;
}

#endif  // LOCKBOX_TASKSTART_H
