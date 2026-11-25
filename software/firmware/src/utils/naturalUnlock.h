#ifndef LOCKBOX_NATURAL_UNLOCK_H
#define LOCKBOX_NATURAL_UNLOCK_H

#include <cstdint>
#include <cstring>
#include <mutex>

// The unlock at the end of a session's timer: the wearer's press on Ready to
// unlock (once the dashboard or Chaster has been asked, where the session
// asks), or a local timer that runs out.
//
// The box used to store the session as finished ("completed") the moment that
// unlock was accepted, before the motor moved, and Motor::unlock stored that
// the lock had ended. When the paddle then did not open, the check after the
// movement showed the unlock-error page over a session the box had already
// finished. No "unlocked" report was sent, because that needs an open paddle.
// Every later message for the session was ignored, because a session the box
// has finished ignores the dashboard (boxEndedSession). Time added after that
// never reached the box: a production box opened on its old end time, eleven
// hours early (rad-app#2799).
//
// So this unlock ends the session in two steps, and nothing is stored before
// the second:
//
//  1. The accept guard (acceptExpiredTimer) checks the session and notes here
//     that the unlock was accepted. The unlock action that follows it tells
//     the paddle to open with Motor::unlock(false), which stores no end of
//     the lock. It clears the held mark, as every movement that opens the
//     paddle does (utils/heldLock.h).
//  2. The check after the movement (LKBX::startPreflight) reads the paddle.
//     Open: the session is stored as finished and the lock as ended, and the
//     box reports "unlocked" on its way home. Still closed: the held mark the
//     lock had is put back, the motor presses the paddle home again, and the
//     unlock-error page stands over a session that is still running. The
//     dashboard's messages for it apply as on any locked page, and a restart
//     restores the lock.
//
// What is noted here lives in RAM. A restart between the two steps finds a
// stored lock that is not finished and closes the box again; the wearer
// unlocks once more from Ready to unlock.
//
// No Arduino dependencies, so it is unit-tested natively. See
// test/test_natural_unlock.
namespace naturalUnlock {

    // An accepted unlock is carried out by the unlock action of the same
    // transition, or by the Unlock event the Chaster answer sends right after
    // accepting, which can wait behind a motor movement or two. One that is
    // still waiting after this long was never carried out, and must not turn
    // a later, unrelated unlock into this one.
    constexpr uint32_t kAcceptedForMs = 15000;

    // What the check after the motor movement found.
    enum class Check : uint8_t {
        // No unlock of this kind is under way: the check decides as for any
        // other movement (decidePreflight).
        NOT_UNDER_WAY,
        // The paddle reads open: the session is finished.
        OPENED,
        // The paddle still reads closed: the session runs on.
        STILL_CLOSED,
    };

    // `locked` is the stored lock. A dashboard clear that landed since the
    // paddle was told to open has ended it already, and the check is then the
    // one of any unlocked box.
    inline Check checkPaddle(bool underWay, bool locked, bool paddleLocked) {
        if (!underWay || !locked) return Check::NOT_UNDER_WAY;
        return paddleLocked ? Check::STILL_CLOSED : Check::OPENED;
    }

    // What the check learns about the unlock it follows.
    struct Opening {
        bool underWay = false;
        // The held mark as it was before the paddle was told to open.
        bool wasHeld = false;
    };

    // Used from the task that dispatches the unlock (buttons, the countdown,
    // the delay task), the delay task (the check) and the animation task;
    // every operation holds the mutex.
    class Tracker {
      public:
        // The accept guard found the session ready to end.
        void accept(uint32_t nowMs) {
            std::lock_guard<std::mutex> lock(m);
            stage = Stage::ACCEPTED;
            acceptedMs = nowMs;
        }

        // Whether an accepted unlock is waiting for its unlock action. The
        // actions in front of that one (clearLocalPlay) store nothing while
        // it is.
        bool isAccepted(uint32_t nowMs) const {
            std::lock_guard<std::mutex> lock(m);
            return acceptedAt(nowMs);
        }

        // An unlock action is about to move the motor. True when it carries
        // out the accepted unlock, which is used up either way.
        bool begin(uint32_t nowMs) {
            std::lock_guard<std::mutex> lock(m);
            const bool accepted = acceptedAt(nowMs);
            reset();
            return accepted;
        }

        // The paddle has been told to open for it. `motorMovement` is the
        // motor's count of movements (Motor::lockId) with this one counted:
        // any later movement, whatever asked for it, ends this unlock.
        void opening(int motorMovement, bool wasHeld) {
            std::lock_guard<std::mutex> lock(m);
            stage = Stage::OPENING;
            movement = motorMovement;
            held = wasHeld;
        }

        // Whether the last motor movement was this unlock, still unchecked.
        bool isOpening(int motorMovement) const {
            std::lock_guard<std::mutex> lock(m);
            return stage == Stage::OPENING && movement == motorMovement;
        }

        // The check after a motor movement. Tells it whether that movement
        // was this unlock, and forgets the unlock: each one is checked once.
        Opening take(int motorMovement) {
            std::lock_guard<std::mutex> lock(m);
            Opening result;
            result.underWay =
                stage == Stage::OPENING && movement == motorMovement;
            result.wasHeld = result.underWay && held;
            // An unlock accepted for the next transition is not this one's.
            if (stage == Stage::OPENING) reset();
            return result;
        }

        void clear() {
            std::lock_guard<std::mutex> lock(m);
            reset();
        }

      private:
        enum class Stage : uint8_t { IDLE, ACCEPTED, OPENING };

        // Unsigned subtraction keeps this correct across millis() wraparound.
        bool acceptedAt(uint32_t nowMs) const {
            return stage == Stage::ACCEPTED &&
                   static_cast<uint32_t>(nowMs - acceptedMs) < kAcceptedForMs;
        }
        void reset() {
            stage = Stage::IDLE;
            acceptedMs = 0;
            movement = 0;
            held = false;
        }

        mutable std::mutex m;
        Stage stage = Stage::IDLE;
        uint32_t acceptedMs = 0;
        int movement = 0;
        bool held = false;
    };

    inline Tracker tracker;

}  // namespace naturalUnlock

// Whether the box has ended the stored session itself: it declined the offer
// ("abandoned"), or it opened at the end of the timer ("completed"). Such a
// session ignores what the dashboard says about it, so that a repeated or
// late message cannot undo the wearer's decision or lock an open box again.
//
// That is only right once the paddle has opened. A session whose unlock
// failed is not stored as finished, so this is false for it and the
// dashboard's messages apply.
inline bool boxEndedSession(bool sameSession, const char* savedState) {
    if (!sameSession || savedState == nullptr) return false;
    return strcmp(savedState, "abandoned") == 0 ||
           strcmp(savedState, "completed") == 0;
}

#endif  // LOCKBOX_NATURAL_UNLOCK_H
