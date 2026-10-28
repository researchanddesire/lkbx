#pragma once
#include "FreeRTOS.h"
#include <cstddef>
struct MockQueue;
using QueueHandle_t = MockQueue *;
QueueHandle_t xQueueCreate(unsigned, size_t);
BaseType_t xQueueSend(QueueHandle_t, const void *, TickType_t);
BaseType_t xQueueReceive(QueueHandle_t, void *, TickType_t);
