#ifndef LOCKBOX_UNSENT_REPORT_H
#define LOCKBOX_UNSENT_REPORT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

// A state report is sealed just before it is sent, and sealing needs a little
// internal RAM. Right after the MQTT client has connected again that RAM can
// be gone for a moment (2026-11-16 rig run on R8: `esp-aes: Failed to allocate
// memory`, six seconds after leaving the Wi-Fi page). The report was dropped:
// the box unlocked and the dashboard never heard of it.
//
// So the newest state report that could not go out is kept, in plain form,
// and sealed and sent again a little later. Only one is kept: any state
// report made after it supersedes it. The retries are bounded, and they are
// only spent while the box is connected.
//
// Keeping the report must not need internal RAM at the very moment there is
// none, and must never throw. So it is copied into storage the caller hands
// over once (from PSRAM on the device), and nothing here allocates.
//
// LOCKED reports have their own tracker (utils/lockedReport.h), which also
// waits for the broker's acknowledgement. No Arduino dependencies, so it is
// unit-tested natively. See test/test_unsent_report.
namespace unsentReport {

    constexpr uint32_t kRetryIntervalMs = 5 * 1000;
    constexpr uint8_t kMaxAttempts = 12;  // a minute of retries
    // A state report is about 450 bytes.
    constexpr size_t kCapacity = 1024;

    // Used from the tasks that report (state machine actions) and the
    // activity log task (retries); every operation holds the mutex.
    class Holder {
      public:
        // Where the held report is kept: `capacity` bytes that stay valid for
        // good. False when storage was already given; the caller keeps
        // ownership of this buffer then.
        bool setStorage(char *buffer, size_t capacity) {
            std::lock_guard<std::mutex> lock(m);
            if (storage != nullptr || buffer == nullptr || capacity == 0) {
                return false;
            }
            storage = buffer;
            storageBytes = capacity;
            return true;
        }

        bool hasStorage() const {
            std::lock_guard<std::mutex> lock(m);
            return storage != nullptr;
        }

        // A state report could not be sealed or queued. It replaces whatever
        // was held: the newest state wins. False when it could not be kept
        // (no storage, or too long); nothing is held then, because the
        // report held before is out of date either way.
        bool hold(const char *report, size_t length, uint32_t nowMs) {
            std::lock_guard<std::mutex> lock(m);
            reset();
            if (storage == nullptr || report == nullptr || length == 0 ||
                length >= storageBytes) {
                return false;
            }
            memcpy(storage, report, length);
            storage[length] = '\0';
            heldBytes = length;
            lastAttemptMs = nowMs;
            return true;
        }

        // A newer state report is on its way: the held one is out of date.
        void clear() {
            std::lock_guard<std::mutex> lock(m);
            reset();
        }

        bool due(uint32_t nowMs) const {
            std::lock_guard<std::mutex> lock(m);
            // Unsigned subtraction keeps this correct across millis()
            // wraparound.
            return heldBytes != 0 &&
                   nowMs - lastAttemptMs >= kRetryIntervalMs;
        }

        // Copies the held report out for a retry. `forTicket` names it for
        // stillHeld() and retried(). False when nothing is held, or the copy
        // came out short (a string type that could not allocate).
        template <typename TString>
        bool read(TString &report, uint32_t &forTicket) const {
            std::lock_guard<std::mutex> lock(m);
            forTicket = ticket;
            if (heldBytes == 0) return false;
            report = storage;
            return report.length() == heldBytes;
        }

        // Whether the report a retry is working on is still the one to send.
        // Checked just before it is queued, so a report that was superseded
        // while it was being sealed is not sent after its successor.
        bool stillHeld(uint32_t forTicket) const {
            std::lock_guard<std::mutex> lock(m);
            return heldBytes != 0 && ticket == forTicket;
        }

        // A retry ran. Sent, the report is done with. Otherwise it stays for
        // the next retry, until the budget is spent.
        void retried(uint32_t forTicket, bool sent, uint32_t nowMs) {
            std::lock_guard<std::mutex> lock(m);
            if (heldBytes == 0 || ticket != forTicket) return;
            if (sent) {
                reset();
                return;
            }
            lastAttemptMs = nowMs;
            if (++attempts >= kMaxAttempts) reset();
        }

        bool isHeld() const {
            std::lock_guard<std::mutex> lock(m);
            return heldBytes != 0;
        }
        uint8_t attemptCount() const {
            std::lock_guard<std::mutex> lock(m);
            return attempts;
        }

      private:
        void reset() {
            heldBytes = 0;
            attempts = 0;
            ++ticket;
        }

        mutable std::mutex m;
        char *storage = nullptr;
        size_t storageBytes = 0;
        // Zero: nothing is held.
        size_t heldBytes = 0;
        uint8_t attempts = 0;
        uint32_t lastAttemptMs = 0;
        // Changes whenever the held report does.
        uint32_t ticket = 0;
    };

    inline Holder holder;

}  // namespace unsentReport

#endif  // LOCKBOX_UNSENT_REPORT_H
