#ifndef LOCKBOX_EMERGENCY_REPORT_H
#define LOCKBOX_EMERGENCY_REPORT_H

#include <cstdint>
#include <mutex>
#include <string>

// The EMERGENCY report tells the dashboard that the wearer used the emergency
// unlock: the dashboard ends the session and starts its own 24 hour lockout.
// It was sent once. If it was lost (no connection at that moment, no memory
// to seal it) the dashboard kept the session as locked for good, and a box
// that restarted online was told so (utils/emergencyRecord.h).
//
// So the report is owed until the broker has acknowledged it. It is kept in
// flash in plain form (services write it; this is the part in RAM), so it
// survives a restart, and the same bytes are sealed and sent again: the
// dashboard knows a report by its id and takes a second copy as a duplicate.
// It is sent when the box is connected, again every ten seconds without an
// acknowledgement, twelve times per connection at most; a new connection, or
// the dashboard showing that it still has not applied it, starts a new round.
//
// LOCKED has its own tracker (utils/lockedReport.h) and every other report
// the unsent-report holder (utils/unsentReport.h), which keeps only the
// newest report and only in RAM: neither may replace this one. No Arduino
// dependencies, so it is unit-tested natively. See test/test_emergency_record.
namespace emergencyReport {

    constexpr uint32_t kResendIntervalMs = 10 * 1000;
    constexpr uint8_t kMaxAttempts = 12;  // per connection

    // Used from state machine actions and the state read (a report becomes
    // owed), the activity log task (sending) and the MQTT task
    // (acknowledgements); every operation holds the mutex.
    class Owed {
      public:
        // The report is owed (again). The same report that is already owed
        // keeps its place, with a new round of attempts.
        void owe(const std::string &report) {
            std::lock_guard<std::mutex> lock(m);
            if (!owed_ || stored_ != report) {
                stored_ = report;
                msgId_ = -1;
                sentOnce_ = false;
            }
            owed_ = true;
            acknowledged_ = false;
            toRecord_ = false;
            attempts_ = 0;
        }

        // Whether to send it now. Only spent while the box is connected.
        bool due(uint32_t nowMs) const {
            std::lock_guard<std::mutex> lock(m);
            if (!owed_ || stored_.empty() || attempts_ >= kMaxAttempts) {
                return false;
            }
            if (!sentOnce_) return true;
            // Unsigned subtraction keeps this correct across millis()
            // wraparound.
            return static_cast<uint32_t>(nowMs - lastAttemptMs_) >=
                   kResendIntervalMs;
        }

        std::string report() const {
            std::lock_guard<std::mutex> lock(m);
            return stored_;
        }

        // An attempt ran. `id` < 0: it could not be sealed or queued, which
        // does not spend the budget but waits out the interval.
        void onAttempt(int id, uint32_t nowMs) {
            std::lock_guard<std::mutex> lock(m);
            if (!owed_) return;
            sentOnce_ = true;
            lastAttemptMs_ = nowMs;
            if (id < 0) return;
            msgId_ = id;
            if (attempts_ < UINT8_MAX) ++attempts_;
        }

        // A fresh connection gets a fresh budget; the report stays owed.
        void onConnected() {
            std::lock_guard<std::mutex> lock(m);
            if (owed_) attempts_ = 0;
        }

        // The broker acknowledged a publish. Only the tracked id counts.
        // Message ids are 16-bit, so another report's acknowledgement can
        // collide with it (1 in 65535): the dashboard's state then shows the
        // session still active and the report is owed again.
        void onPublished(int id) {
            std::lock_guard<std::mutex> lock(m);
            if (!owed_ || id < 0 || id != msgId_) return;
            owed_ = false;
            acknowledged_ = true;
            toRecord_ = true;
            msgId_ = -1;
        }

        // True once after an acknowledgement: the caller records in flash
        // that the report is no longer owed.
        bool takeAcknowledgement() {
            std::lock_guard<std::mutex> lock(m);
            const bool take = toRecord_;
            toRecord_ = false;
            return take;
        }

        // The dashboard ended the session by itself: nothing is owed any
        // more.
        void drop() {
            std::lock_guard<std::mutex> lock(m);
            owed_ = false;
            acknowledged_ = false;
            toRecord_ = false;
            msgId_ = -1;
            attempts_ = 0;
            stored_.clear();
        }

        bool isOwed() const {
            std::lock_guard<std::mutex> lock(m);
            return owed_;
        }
        // The broker has acknowledged the report that was last owed.
        bool isAcknowledged() const {
            std::lock_guard<std::mutex> lock(m);
            return acknowledged_;
        }
        uint8_t attemptCount() const {
            std::lock_guard<std::mutex> lock(m);
            return attempts_;
        }

      private:
        mutable std::mutex m;
        bool owed_ = false;
        bool acknowledged_ = false;
        // An acknowledgement that has not been recorded in flash yet.
        bool toRecord_ = false;
        // An attempt has been made for this report since it became owed.
        bool sentOnce_ = false;
        int msgId_ = -1;
        uint32_t lastAttemptMs_ = 0;
        uint8_t attempts_ = 0;
        std::string stored_;
    };

    inline Owed owed;

}  // namespace emergencyReport

#endif  // LOCKBOX_EMERGENCY_REPORT_H
