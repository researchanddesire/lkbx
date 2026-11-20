#ifndef LOCKBOX_STATE_READ_H
#define LOCKBOX_STATE_READ_H

#include <stdint.h>

#include <atomic>

// The box reads its state from the dashboard over HTTPS whenever it may have
// missed something: after its MQTT client was stopped for the Wi-Fi setup
// portal, after every MQTT connection that follows a drop, when a clear names
// no session, when a sync names another session than the one it holds, when
// Chaster does not know the session.
//
// The read runs on the delay task, which always exists, in steps that never
// block it (LKBX::serviceStateRead): the HTTP worker takes the request and the
// delay task polls for the answer. It used to run on a task of its own with an
// 8.3 KB stack, and right after the MQTT client had taken its 10 KB back the
// largest free block of internal RAM was 7.7 to 8.2 KB (2026-11-16 rig run),
// so the read started after 1 reconnect in 6.
//
// This is the bookkeeping only: whether a read is wanted, and when to try.
// A read that is wanted is tried until one has completed. It never gives up:
// a box that stopped listening must not keep a session the dashboard ended.
// No Arduino dependencies, so it is unit-tested natively. See
// test/test_state_read.

namespace stateRead {
    // The whole request, including its time in the HTTP worker's queue.
    constexpr int kTimeoutMs = 10000;
    // After a read that failed or could not be handed to the HTTP worker.
    constexpr uint32_t kRetryMs = 5000;
    // After this many failures in a row the box tries less often, so a
    // dashboard that is down is not asked every few seconds for hours.
    constexpr uint8_t kQuickAttempts = 6;
    constexpr uint32_t kSlowRetryMs = 30000;
}  // namespace stateRead

class StateRead {
  public:
    // Any task: the state must be read (again).
    void want() { wanted_.fetch_add(1, std::memory_order_relaxed); }

    // Everything below is for the one task that runs the read.

    // Whether a read is wanted that no completed read has answered.
    bool pending() const {
        return wanted_.load(std::memory_order_relaxed) != served_;
    }

    // Whether to start a read now.
    bool due(uint32_t nowMs) const {
        if (inFlight_ || !pending()) return false;
        // The first read, or the one before it completed: at once.
        if (failures_ == 0) return true;
        // A read wanted since the last attempt is news (the box has just
        // connected again, say), so it does not wait out the slow interval.
        const bool news =
            wanted_.load(std::memory_order_relaxed) != attemptFor_;
        const uint32_t wait =
            (news || failures_ <= stateRead::kQuickAttempts)
                ? stateRead::kRetryMs
                : stateRead::kSlowRetryMs;
        return static_cast<uint32_t>(nowMs - attemptedMs_) >= wait;
    }

    // A read starts. Whatever was wanted up to now is what it answers; a
    // read wanted while it runs gets one of its own.
    void begin(uint32_t nowMs) {
        attemptFor_ = wanted_.load(std::memory_order_relaxed);
        attemptedMs_ = nowMs;
    }

    // The request is with the HTTP worker.
    void sent() { inFlight_ = true; }

    // The read ended. `completed` is false when there was no usable answer,
    // or the request could not be sent at all.
    void finished(bool completed, uint32_t nowMs) {
        inFlight_ = false;
        attemptedMs_ = nowMs;
        if (completed) {
            served_ = attemptFor_;
            failures_ = 0;
        } else if (failures_ < UINT8_MAX) {
            ++failures_;
        }
    }

    bool inFlight() const { return inFlight_; }
    uint8_t failures() const { return failures_; }

  private:
    // How many reads were asked for. Only ever compared for equality.
    std::atomic<uint32_t> wanted_{0};
    // The count the last completed read answered.
    uint32_t served_ = 0;
    // The count the read in flight, or the last one tried, answers.
    uint32_t attemptFor_ = 0;
    uint32_t attemptedMs_ = 0;
    uint8_t failures_ = 0;
    bool inFlight_ = false;
};

#endif  // LOCKBOX_STATE_READ_H
