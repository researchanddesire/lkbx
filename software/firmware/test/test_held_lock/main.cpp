#include <initializer_list>

#include "unity.h"

#include "../../src/utils/heldLock.h"

// The check after every motor movement, and what it decides for a lock the
// box already holds. The box under test keeps the one mark the firmware keeps
// (lockbox/lockHeld), set and cleared at the same moments: set when the check
// confirms a lock, cleared whenever the paddle is told to open.

void setUp(void) {}
void tearDown(void) {}

namespace {

struct Box {
    bool locked = false;    // the stored lock (lockbox/isLocked)
    bool lockHeld = false;  // the mark
    bool paddleOpened = false;

    // LKBX::startPreflight with these sensor readings.
    Preflight check(bool paddleLocked, bool backplateClosed) {
        const Preflight result =
            decidePreflight(locked, paddleLocked, backplateClosed, lockHeld);
        if (result == Preflight::LOCK_CONFIRMED) lockHeld = true;
        // The error pages of a lock that never closed open the paddle.
        if (result == Preflight::CANNOT_LOCK ||
            result == Preflight::NO_BACKPLATE) {
            openPaddle();
        }
        return result;
    }
    // Motor::lock(true): the stored lock begins.
    void startLock() {
        if (!locked) lockHeld = false;
        locked = true;
    }
    // Motor::unlock(false): a break, an offer, an error page.
    void openPaddle() {
        lockHeld = false;
        paddleOpened = true;
    }
    // Motor::unlock(true): the lock ends.
    void unlock() {
        openPaddle();
        locked = false;
    }
};

Box heldLock() {
    Box box;
    box.startLock();
    TEST_ASSERT_EQUAL(Preflight::LOCK_CONFIRMED, box.check(true, true));
    box.paddleOpened = false;
    return box;
}

}  // namespace

// --- decidePreflight --------------------------------------------------------

void test_a_locked_box_with_agreeing_sensors_is_confirmed(void) {
    TEST_ASSERT_EQUAL(Preflight::LOCK_CONFIRMED,
                      decidePreflight(true, true, true, false));
    TEST_ASSERT_EQUAL(Preflight::LOCK_CONFIRMED,
                      decidePreflight(true, true, true, true));
}

void test_a_new_lock_that_cannot_close_reports_why(void) {
    TEST_ASSERT_EQUAL(Preflight::CANNOT_LOCK,
                      decidePreflight(true, false, true, false));
    TEST_ASSERT_EQUAL(Preflight::CANNOT_LOCK,
                      decidePreflight(true, false, false, false));
    TEST_ASSERT_EQUAL(Preflight::NO_BACKPLATE,
                      decidePreflight(true, true, false, false));
}

void test_a_held_lock_stays_closed_whatever_the_sensors_say(void) {
    TEST_ASSERT_EQUAL(Preflight::KEEP_CLOSED,
                      decidePreflight(true, false, true, true));
    TEST_ASSERT_EQUAL(Preflight::KEEP_CLOSED,
                      decidePreflight(true, true, false, true));
    TEST_ASSERT_EQUAL(Preflight::KEEP_CLOSED,
                      decidePreflight(true, false, false, true));
}

void test_an_unlocked_box_is_checked_as_before(void) {
    for (const bool held : {false, true}) {
        for (const bool backplate : {false, true}) {
            TEST_ASSERT_EQUAL(Preflight::PADDLE_OPEN,
                              decidePreflight(false, false, backplate, held));
            TEST_ASSERT_EQUAL(Preflight::CANNOT_UNLOCK,
                              decidePreflight(false, true, backplate, held));
        }
    }
}

// --- A lock across restarts --------------------------------------------------

// Every boot of a locked box runs the check again. One bad reading of the
// paddle sensors used to open the paddle and wait on an error page.
void test_a_restart_with_a_bad_paddle_reading_keeps_the_box_closed(void) {
    Box box = heldLock();
    TEST_ASSERT_EQUAL(Preflight::KEEP_CLOSED, box.check(false, true));
    TEST_ASSERT_FALSE(box.paddleOpened);
    TEST_ASSERT_TRUE(box.locked);
    // The mark survives the bad reading, so the next restart is safe too.
    TEST_ASSERT_EQUAL(Preflight::KEEP_CLOSED, box.check(false, true));
    TEST_ASSERT_FALSE(box.paddleOpened);
}

// The same for the backplate sensors, and for both at once.
void test_a_restart_with_the_backplate_reading_open_keeps_the_box_closed(void) {
    Box box = heldLock();
    TEST_ASSERT_EQUAL(Preflight::KEEP_CLOSED, box.check(true, false));
    TEST_ASSERT_FALSE(box.paddleOpened);
    TEST_ASSERT_EQUAL(Preflight::KEEP_CLOSED, box.check(false, false));
    TEST_ASSERT_FALSE(box.paddleOpened);
    // Sensors that read well again confirm it as usual.
    TEST_ASSERT_EQUAL(Preflight::LOCK_CONFIRMED, box.check(true, true));
}

// A lock being started must still be able to fail: the wearer left the
// backplate off and has to get the paddle out of the way to fit it.
void test_a_first_lock_without_its_backplate_still_opens(void) {
    Box box;
    box.startLock();
    TEST_ASSERT_EQUAL(Preflight::NO_BACKPLATE, box.check(true, false));
    TEST_ASSERT_TRUE(box.paddleOpened);
    TEST_ASSERT_FALSE(box.lockHeld);
    // Restart on the error page: still a lock that never closed.
    box.paddleOpened = false;
    TEST_ASSERT_EQUAL(Preflight::NO_BACKPLATE, box.check(true, false));
    TEST_ASSERT_TRUE(box.paddleOpened);
}

// A break opens the paddle. Closing the box again is a new check: with the
// backplate still off, the paddle must open so it can be fitted.
void test_the_lock_after_a_break_is_checked_as_a_new_one(void) {
    Box box = heldLock();
    box.openPaddle();  // locked.break
    box.paddleOpened = false;
    TEST_ASSERT_TRUE(box.locked);
    TEST_ASSERT_EQUAL(Preflight::NO_BACKPLATE, box.check(true, false));
    TEST_ASSERT_TRUE(box.paddleOpened);
    // With the backplate on it is confirmed, and held again.
    box.paddleOpened = false;
    TEST_ASSERT_EQUAL(Preflight::LOCK_CONFIRMED, box.check(true, true));
    TEST_ASSERT_EQUAL(Preflight::KEEP_CLOSED, box.check(false, true));
    TEST_ASSERT_FALSE(box.paddleOpened);
}

// The mark of one lock never covers the next.
void test_a_new_lock_does_not_inherit_the_mark_of_the_last(void) {
    Box box = heldLock();
    box.unlock();
    TEST_ASSERT_FALSE(box.lockHeld);
    box.paddleOpened = false;
    box.startLock();
    TEST_ASSERT_EQUAL(Preflight::CANNOT_LOCK, box.check(false, true));
    TEST_ASSERT_TRUE(box.paddleOpened);

    // Even a mark left behind by a lock that was cleared without the motor
    // moving is dropped when the next lock is stored.
    Box stale;
    stale.lockHeld = true;
    stale.startLock();
    TEST_ASSERT_EQUAL(Preflight::NO_BACKPLATE, stale.check(true, false));
}

int runUnityTests(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_locked_box_with_agreeing_sensors_is_confirmed);
    RUN_TEST(test_a_new_lock_that_cannot_close_reports_why);
    RUN_TEST(test_a_held_lock_stays_closed_whatever_the_sensors_say);
    RUN_TEST(test_an_unlocked_box_is_checked_as_before);
    RUN_TEST(test_a_restart_with_a_bad_paddle_reading_keeps_the_box_closed);
    RUN_TEST(test_a_restart_with_the_backplate_reading_open_keeps_the_box_closed);
    RUN_TEST(test_a_first_lock_without_its_backplate_still_opens);
    RUN_TEST(test_the_lock_after_a_break_is_checked_as_a_new_one);
    RUN_TEST(test_a_new_lock_does_not_inherit_the_mark_of_the_last);
    return UNITY_END();
}

int main(void) { return runUnityTests(); }
