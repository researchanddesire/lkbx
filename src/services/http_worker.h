#ifndef LOCKBOX_HTTP_WORKER_H
#define LOCKBOX_HTTP_WORKER_H

#include <ArduinoJson.h>
#include <cstdint>

void initHttpWorker();
// False means the active client is still cleaning up: defer OTA until a later
// stop succeeds. IDF header parsing can exceed a per-read timeout under a slow
// trickle, so shutdown waits at most 3 seconds and never forcibly deletes a task.
bool stopHttpWorker();

// Uses only RAD_SERVER/api/health over verified HTTPS, HTTP 200, and exactly one
// valid GMT Date header (2024..2099). No JSON, NTP or redirect fallback.
bool getAuthenticatedServerTimeMs(uint64_t *out);

// timeoutMs is a total deadline, including queue time and up to two retries,
// capped at 60 seconds. Queue saturation, cancellation, invalid URLs, incomplete
// or oversized responses and transport failures return -1 with an empty body.
// Only HTTPS is accepted; Development is restricted to its configured origin.

int get(const String &url, String *responsePayload = nullptr,
        int timeoutMs = 5000, int retries = 0);

int post(const String &url, const String &body,
         String *responsePayload = nullptr, int timeoutMs = 5000,
         int retries = 0);

int post(const String &url, const JsonDocument &doc,
         String *responsePayload = nullptr, int timeoutMs = 5000,
         int retries = 0);

#endif  // LOCKBOX_HTTP_WORKER_H
