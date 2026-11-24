#ifndef LOCKBOX_DECISION_REPORT_H
#define LOCKBOX_DECISION_REPORT_H

#include <cstdint>
#include <mutex>
#include <string>

// Two decisions are kept in flash with the session they end, and reported
// until the dashboard has taken them: ABANDONED once an offer was declined on
// the box, and UNLOCKED once the box opened at the end of its timer. The MQTT
// outbox alone cannot carry them through an offline sleep or a restart, and
// the dashboard can fail to act on a report it did receive.
//
// The dashboard answers by clearing that session or by sending another one.
// Either way the box no longer holds the decision, and that is the
// acknowledgement: nothing is left to repeat.
//
// The report used to be built afresh every ten seconds, for as long as the
// answer did not come. Each copy had a new id, so the dashboard stored each
// one: 16,308 segments from one box in two and a half days (rad-app#2782).
//
// So the repeated report is built once per decision and the same bytes are
// sent again: the dashboard knows a report by its id and takes a second copy
// as a duplicate. The wait doubles after every attempt, from ten seconds up
// to five minutes. It never gives up, because the dashboard may only be able
// to act later (the session's end time there has not passed yet), but an
// unanswered box is down to one report every five minutes within ten.
//
// The page that makes the decision reports it once itself, at that moment
// (the decline, or the check after the paddle has opened). The dashboard
// normally answers that report within a second, so the first repeat waits ten
// seconds like any other: two reports sent together would both be handled.
//
// No Arduino dependencies, so it is unit-tested natively. See
// test/test_decision_report.
namespace decisionReport {

    constexpr uint32_t kFirstWaitMs = 10 * 1000;
    constexpr uint32_t kLongestWaitMs = 5 * 60 * 1000;

    // The wait after the given number of attempts: 10 s, 20 s, 40 s, ...
    inline uint32_t waitAfter(uint8_t attempts) {
        if (attempts == 0) return 0;
        uint32_t wait = kFirstWaitMs;
        for (uint8_t made = 1; made < attempts; ++made) {
            if (wait >= kLongestWaitMs / 2) return kLongestWaitMs;
            wait *= 2;
        }
        return wait < kLongestWaitMs ? wait : kLongestWaitMs;
    }

    // Used from the activity log task (sending) and the MQTT task (a new
    // connection); every operation holds the mutex.
    class Tracker {
      public:
        // The box holds `state` as its decision for `sessionId`. True when
        // its report should be sent now. Another session or another decision
        // is a new report: its page has just reported it, which counts as the
        // first attempt, so the first repeat waits out the first interval.
        bool due(const char *sessionId, const char *state, uint32_t nowMs) {
            std::lock_guard<std::mutex> lock(m);
            if (sessionId == nullptr || state == nullptr ||
                sessionId[0] == '\0' || state[0] == '\0') {
                reset();
                return false;
            }
            if (session_ != sessionId || state_ != state) {
                reset();
                session_ = sessionId;
                state_ = state;
                attempts_ = 1;
                lastAttemptMs_ = nowMs;
                return false;
            }
            // A new connection: at once.
            if (attempts_ == 0) return true;
            // Unsigned subtraction keeps this correct across millis()
            // wraparound.
            return static_cast<uint32_t>(nowMs - lastAttemptMs_) >=
                   waitAfter(attempts_);
        }

        // The report to send again. Empty until the first attempt has built
        // one: the caller then builds it and hands it to onAttempt.
        std::string payload() const {
            std::lock_guard<std::mutex> lock(m);
            return stored_;
        }

        // An attempt ran with `payload`. `id` < 0: it could not be sealed or
        // queued. The report is kept for the next attempt, which waits as
        // long as this one did: a send that never left does not lengthen the
        // wait.
        void onAttempt(int id, uint32_t nowMs, const std::string &payload) {
            std::lock_guard<std::mutex> lock(m);
            if (session_.empty()) return;
            if (stored_.empty()) stored_ = payload;
            lastAttemptMs_ = nowMs;
            if (id < 0) {
                if (attempts_ == 0) attempts_ = 1;
                return;
            }
            if (attempts_ < UINT8_MAX) ++attempts_;
        }

        // A new connection: whatever was sent on the last one may be lost, so
        // the report goes out at once and the wait starts short again. The
        // report itself, and its id, stay as they are.
        void onConnected() {
            std::lock_guard<std::mutex> lock(m);
            if (!session_.empty()) attempts_ = 0;
        }

        // The box holds no decision any more: the dashboard cleared the
        // session or sent another one. Nothing is left to repeat.
        void clear() {
            std::lock_guard<std::mutex> lock(m);
            reset();
        }

        bool isTracking() const {
            std::lock_guard<std::mutex> lock(m);
            return !session_.empty();
        }
        uint8_t attemptCount() const {
            std::lock_guard<std::mutex> lock(m);
            return attempts_;
        }

      private:
        void reset() {
            session_.clear();
            state_.clear();
            stored_.clear();
            attempts_ = 0;
            lastAttemptMs_ = 0;
        }

        mutable std::mutex m;
        std::string session_;
        std::string state_;
        std::string stored_;
        uint8_t attempts_ = 0;
        uint32_t lastAttemptMs_ = 0;
    };

    inline Tracker tracker;

}  // namespace decisionReport

#endif  // LOCKBOX_DECISION_REPORT_H
