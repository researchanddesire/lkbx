#ifndef LOCKBOX_LOCKED_REPORT_H
#define LOCKBOX_LOCKED_REPORT_H

#include <cstdint>
#include <mutex>
#include <string>

// The device reports LOCKED once. The MQTT outbox drops an undelivered
// message after 30 s and an unsynced clock skips the send, so a lost report
// leaves the server unaware of a finished break until the next wake or button
// press. Track the last LOCKED report and repeat the same bytes until the
// broker acknowledges it, then stop. Deep sleep is not covered: the tracker
// lives in RAM, and the boot-time preflight re-reports the physical state.
namespace lockedReport {

    constexpr uint32_t kResendIntervalMs = 10 * 1000;
    constexpr uint8_t kMaxAttempts = 12;  // ~2 min of retries

    // Used from the state machine task (reports), the activity task (retries)
    // and the MQTT task (acknowledgements); every operation holds the mutex.
    class Tracker {
      public:
        // The state machine sent a LOCKED report. Starts a fresh retry budget.
        // `id` < 0 with an empty payload means nothing was enqueued (no client
        // or no clock yet); it stays pending but does not count as an attempt.
        void onReport(int id, uint32_t nowMs, std::string payload) {
            std::lock_guard<std::mutex> lock(m);
            pending = true;
            msgId = id;
            lastAttemptMs = nowMs;
            attempts = id >= 0 ? 1 : 0;
            stored = std::move(payload);
        }

        // A retry ran. It re-enqueues the stored bytes so the server sees the
        // same event (same id, same timestamp) and rejects the duplicates.
        // A retry that could not send (`id` < 0) does not spend the budget.
        void onRetry(int id, uint32_t nowMs, const std::string &payload) {
            std::lock_guard<std::mutex> lock(m);
            if (!pending) return;
            msgId = id;
            lastAttemptMs = nowMs;
            if (id >= 0) attempts++;
            if (stored.empty()) stored = payload;
        }

        // The broker acknowledged a publish. Only the tracked id confirms the
        // report. Message ids are random 16-bit values, so an activity-log
        // ack can collide with the tracked id (1 in 65535); a false confirm
        // only costs the retry, and the boot-time report remains.
        void onPublished(int id) {
            std::lock_guard<std::mutex> lock(m);
            if (!pending || id < 0 || id != msgId) return;
            reset();
        }

        // Any other state message (unlocked, break, ...) supersedes the
        // report, as does entering a break the device never announced.
        void clear() {
            std::lock_guard<std::mutex> lock(m);
            reset();
        }

        // A fresh connection gets a fresh budget; the report stays pending.
        void onConnected() {
            std::lock_guard<std::mutex> lock(m);
            if (pending) attempts = 0;
        }

        bool dueForResend(uint32_t nowMs) const {
            std::lock_guard<std::mutex> lock(m);
            if (!pending || attempts >= kMaxAttempts) return false;
            // Unsigned subtraction keeps this correct across millis()
            // wraparound.
            return nowMs - lastAttemptMs >= kResendIntervalMs;
        }

        // Empty when the first attempt never produced a message.
        std::string payload() const {
            std::lock_guard<std::mutex> lock(m);
            return stored;
        }

        bool isPending() const {
            std::lock_guard<std::mutex> lock(m);
            return pending;
        }
        int trackedId() const {
            std::lock_guard<std::mutex> lock(m);
            return msgId;
        }
        uint8_t attemptCount() const {
            std::lock_guard<std::mutex> lock(m);
            return attempts;
        }

      private:
        void reset() {
            pending = false;
            msgId = -1;
            attempts = 0;
            stored.clear();
        }

        mutable std::mutex m;
        bool pending = false;
        int msgId = -1;
        uint32_t lastAttemptMs = 0;
        uint8_t attempts = 0;
        std::string stored;
    };

    inline Tracker tracker;

}  // namespace lockedReport

#endif  // LOCKBOX_LOCKED_REPORT_H
