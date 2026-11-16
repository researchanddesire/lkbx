#ifndef LOCKBOX_HTTP_WORKER_H
#define LOCKBOX_HTTP_WORKER_H

#include <ArduinoJson.h>
#include <cstdint>

void initHttpWorker();
// False means the active client is still cleaning up: defer OTA until a later
// stop succeeds. IDF header parsing can exceed a per-read timeout under a slow
// trickle, so shutdown waits at most 3 seconds and never forcibly deletes a task.
bool stopHttpWorker();

// Uses only RAD_SERVER/api/health-check over verified HTTPS, HTTP 200, and exactly one
// valid GMT Date header (2024..2099). No JSON, NTP or redirect fallback.
bool getAuthenticatedServerTimeMs(uint64_t *out);

// timeoutMs is a total deadline, including queue time and up to two retries,
// capped at 60 seconds. Queue saturation, cancellation, invalid URLs, incomplete
// or oversized responses and transport failures return -1 with an empty body.
// Only HTTPS is accepted; Development is restricted to its configured origin.

int get(const String &url, String *responsePayload = nullptr,
        int timeoutMs = 5000, int retries = 0);

// Sends the device secret as x-lkbx-device-secret. Only the configured RAD
// server may receive it: any other origin returns -1 without a request.
int getWithDeviceSecret(const String &url, const String &deviceSecret,
                        String *responsePayload = nullptr, int timeoutMs = 5000,
                        int retries = 0);

// Sends the v2 device token as x-lkbx-device-token, to the configured RAD
// server only. An empty token returns -1 without a request.
int getWithDeviceToken(const String &url, const String &deviceToken,
                       String *responsePayload = nullptr, int timeoutMs = 5000,
                       int retries = 0);

int post(const String &url, const String &body,
         String *responsePayload = nullptr, int timeoutMs = 5000,
         int retries = 0);

int post(const String &url, const JsonDocument &doc,
         String *responsePayload = nullptr, int timeoutMs = 5000,
         int retries = 0);

// A request the caller polls instead of waiting for, so a task with other work
// to do (the delay task asking Chaster or reading the state) is never blocked
// on the network. The same checks, limits and total deadline as the waiting
// calls apply, without retries.
struct HttpPending;

// Queues the request. Null when it could not be queued: an invalid request, no
// memory, a full queue or a stopped worker.
HttpPending *postDetached(const String &url, const JsonDocument &doc,
                          int timeoutMs);

// The same for a GET that carries the device's identity, as
// getWithDeviceSecret and getWithDeviceToken do: the delay task reading the
// box's state. Only the configured RAD server may receive either value.
HttpPending *getDetachedWithDeviceSecret(const String &url,
                                         const String &deviceSecret,
                                         int timeoutMs);
HttpPending *getDetachedWithDeviceToken(const String &url,
                                        const String &deviceToken,
                                        int timeoutMs);

// True once the request has ended: answered, failed, or past its deadline.
// `code` and `responsePayload` then hold what post() would have returned, and
// `pending` is released and set to null. False while it is still running.
bool takeHttpResult(HttpPending *&pending, int &code, String *responsePayload);

// Stops waiting for a request whose answer is no longer wanted, and sets
// `pending` to null. The worker drops the request.
void abandonHttp(HttpPending *&pending);

#endif  // LOCKBOX_HTTP_WORKER_H
