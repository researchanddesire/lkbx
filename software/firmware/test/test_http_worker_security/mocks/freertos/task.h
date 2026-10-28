#pragma once
#include "FreeRTOS.h"
using TaskHandle_t = void *;
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *, uint32_t,
                                 void *, UBaseType_t, TaskHandle_t *, BaseType_t);
void vTaskDelay(TickType_t);
void vTaskDelete(TaskHandle_t);
// Deliberately no task notification APIs: real worker must not use them.
