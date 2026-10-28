#pragma once

#include <cstdint>
#include <functional>

// USB host detection also protects benches whose VBUS sense line is absent.
bool sleepPowerConnected();
void initSerialWake();
void enterSleep(uint64_t wakeDelayMs,
                const std::function<void()>& beforeDeepSleep = {});
