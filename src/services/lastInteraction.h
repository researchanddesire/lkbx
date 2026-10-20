#ifndef LOCKBOX_LASTINTERACTION_H
#define LOCKBOX_LASTINTERACTION_H

#include <Arduino.h>
#include <esp_timer.h>

#include "constants/LogTags.h"

#ifdef SHORT_TIMEOUTS
static const unsigned long SLEEP_TIMEOUT = 30000;         // 30 seconds
static const unsigned long PSEUDO_SLEEP_TIMEOUT = 10000;  // 10 seconds
static const unsigned long IDLE_TIMEOUT = 5000;           // 5 seconds
#else
static const unsigned long SLEEP_TIMEOUT = 60000 * 10;        // 5 minutes
static const unsigned long PSEUDO_SLEEP_TIMEOUT = 60000 * 2;  // 2 minutes
static const unsigned long IDLE_TIMEOUT = 30000;              // 30 seconds
#endif

enum class IdleState { SLEEP, PSEUDO_SLEEP, IDLE, NOT_IDLE };

extern unsigned long sleepDuration;
extern volatile unsigned long lastInteraction;
extern volatile IdleState idleState;

void IRAM_ATTR setNotIdleISR();
void setNotIdle(const char* src);

#endif  // LOCKBOX_LASTINTERACTION_H
