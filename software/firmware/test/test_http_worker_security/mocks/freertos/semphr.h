#pragma once
#include "FreeRTOS.h"
struct MockSemaphore;
using SemaphoreHandle_t = MockSemaphore *;
SemaphoreHandle_t xSemaphoreCreateBinary();
void vSemaphoreDelete(SemaphoreHandle_t);
BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t);
BaseType_t xSemaphoreGive(SemaphoreHandle_t);
