#pragma once
#include <Arduino.h>

// RAM-only lease: boot cannot redisplay a stale persisted code. Authenticated
// refresh is independent of NVS broker credentials and never resets the device.
void acceptPairingCode(const String& code, const String& expiresAt);
String currentPairingCode();
bool pairingCodeRefreshDue();
void markPairingCodeRefreshAttempt();
