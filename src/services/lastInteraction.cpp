#include "lastInteraction.h"

unsigned long sleepDuration = 0;
volatile unsigned long lastInteraction = 0;
volatile IdleState idleState = IdleState::NOT_IDLE;

void IRAM_ATTR setNotIdleISR() {
    lastInteraction =
        static_cast<unsigned long>(esp_timer_get_time() / 1000ULL);
    idleState = IdleState::NOT_IDLE;
}

void setNotIdle(const char* src) {
    lastInteraction = millis();

    if (idleState == IdleState::NOT_IDLE) {
        return;
    }
    idleState = IdleState::NOT_IDLE;
    ESP_LOGD(IDLE_TAG, "Setting idle state to NOT_IDLE, called from %s", src);
}
