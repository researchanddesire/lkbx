#ifndef LOCKBOX_SERVER_ONLINE_H
#define LOCKBOX_SERVER_ONLINE_H

#include <cstdint>
#include <atomic>

// Telemetry only: the last completed HTTPS operation against the configured
// RAD origin succeeded (2xx; the clock probe also requires its valid Date).
// No expiry is inferred. Wi-Fi disconnect invalidates that observation, and a
// response begun before the disconnect cannot restore it after reconnection.
uint32_t beginServerHttpAttempt();
void completeServerHttpAttempt(uint32_t attempt, bool success);
void clearServerOnline();
bool isServerOnline(bool wifiConnected);

extern std::atomic<bool> isTimeSynced;

#endif
