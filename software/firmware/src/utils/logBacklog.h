#ifndef LOCKBOX_LOG_BACKLOG_H
#define LOCKBOX_LOG_BACKLOG_H

#include <stdint.h>

// Activity logs go into the MQTT client's outbox, which keeps a copy of each
// in internal RAM until the broker has it. The client only looks through its
// outbox while it is connected: it sends what is waiting, and it drops what
// is older than 30 seconds the moment it connects (the pinned esp-mqtt,
// mqtt_delete_expired_messages, runs in MQTT_STATE_CONNECTED only). While the
// box is off the broker nothing leaves the outbox at all. A box that is
// offline, or sits on the Wi-Fi setup page with its client paused, kept a
// copy of every log it wrote in that time, most of them only to be dropped
// when it connected again, and each one took internal RAM that TLS and the
// task stacks need.
//
// So while the client is off the broker, logs are only added until the outbox
// holds kOfflineBytes. That keeps the logs of a short drop, which are still
// young enough to be delivered, and bounds the rest. State reports are not
// logs: they are few, they have their own retries, and they are always queued.
//
// No Arduino dependencies, so it is unit-tested natively. See
// test/test_log_backlog.

namespace logBacklog {
    // A sealed activity log is 400 to 700 bytes: room for the last few.
    constexpr int kOfflineBytes = 4 * 1024;
}  // namespace logBacklog

// Whether an activity log is handed to the MQTT client now. `outboxBytes` is
// what its outbox holds already.
inline bool activityLogIsQueued(bool connected, int outboxBytes) {
    if (connected) return true;
    return outboxBytes < logBacklog::kOfflineBytes;
}

#endif  // LOCKBOX_LOG_BACKLOG_H
