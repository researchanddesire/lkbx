#include <unity.h>

#include "../../src/utils/missingTask.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kLook = missingTask::kLookMs;
}  // namespace

// A page whose task runs is left alone, however long it stays up.
void test_a_page_with_its_task_is_left_alone() {
    MissingTask watch;
    for (uint32_t now = 0; now < 20 * kLook; now += 50) {
        TEST_ASSERT_FALSE(watch.due(true, true, now));
    }
    TEST_ASSERT_FALSE(watch.missing());
}

// No page that needs the task is up: nothing to start.
void test_no_page_needs_nothing() {
    MissingTask watch;
    for (uint32_t now = 0; now < 20 * kLook; now += 50) {
        TEST_ASSERT_FALSE(watch.due(false, false, now));
    }
    TEST_ASSERT_FALSE(watch.missing());
}

// The fault: the home menu's task did not fit when the box came back to the
// menu, and the page stayed half drawn.
void test_a_missing_task_is_started_on_the_second_look() {
    MissingTask watch;
    // The first look: the page may be starting its own task right now.
    TEST_ASSERT_FALSE(watch.due(true, false, 1000));
    TEST_ASSERT_TRUE(watch.missing());
    // Between looks nothing is asked.
    TEST_ASSERT_FALSE(watch.due(true, false, 1000 + kLook - 1));
    TEST_ASSERT_TRUE(watch.due(true, false, 1000 + kLook));
}

// Memory can stay short for a while: the task is tried once a look until it
// exists.
void test_it_is_tried_again_at_every_look_while_it_stays_missing() {
    MissingTask watch;
    TEST_ASSERT_FALSE(watch.due(true, false, 0));
    for (uint32_t look = 1; look <= 10; ++look) {
        TEST_ASSERT_FALSE(watch.due(true, false, look * kLook - 1));
        TEST_ASSERT_TRUE(watch.due(true, false, look * kLook));
    }
    // It was started: nothing more.
    TEST_ASSERT_FALSE(watch.due(true, true, 11 * kLook));
    TEST_ASSERT_FALSE(watch.missing());
    TEST_ASSERT_FALSE(watch.due(true, true, 12 * kLook));
}

// A page that is being entered creates its task a moment after the state
// changes. One look that falls into that moment must not start a second one.
void test_a_page_that_starts_its_own_task_is_not_given_a_second() {
    MissingTask watch;
    TEST_ASSERT_FALSE(watch.due(true, false, 5000));
    // By the next look the page's own task is there.
    TEST_ASSERT_FALSE(watch.due(true, true, 5000 + kLook));
    // It ends with its page and a later page misses its task: two looks
    // again.
    TEST_ASSERT_FALSE(watch.due(false, false, 5000 + 2 * kLook));
    TEST_ASSERT_FALSE(watch.due(true, false, 5000 + 3 * kLook));
    TEST_ASSERT_TRUE(watch.due(true, false, 5000 + 4 * kLook));
}

// Leaving the page between the two looks calls it off.
void test_leaving_the_page_calls_it_off() {
    MissingTask watch;
    TEST_ASSERT_FALSE(watch.due(true, false, 0));
    TEST_ASSERT_FALSE(watch.due(false, false, kLook));
    TEST_ASSERT_FALSE(watch.missing());
    TEST_ASSERT_FALSE(watch.due(true, false, 2 * kLook));
    TEST_ASSERT_TRUE(watch.due(true, false, 3 * kLook));
}

// The very first call looks at once, whatever the clock says.
void test_the_first_call_is_a_look() {
    MissingTask watch;
    TEST_ASSERT_FALSE(watch.due(true, false, 0));
    TEST_ASSERT_TRUE(watch.missing());
}

void test_looks_survive_the_millisecond_counter_wrapping() {
    MissingTask watch;
    const uint32_t first = UINT32_MAX - 500;
    TEST_ASSERT_FALSE(watch.due(true, false, first));
    TEST_ASSERT_FALSE(watch.due(true, false, first + kLook - 1));
    TEST_ASSERT_TRUE(watch.due(true, false, first + kLook));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_page_with_its_task_is_left_alone);
    RUN_TEST(test_no_page_needs_nothing);
    RUN_TEST(test_a_missing_task_is_started_on_the_second_look);
    RUN_TEST(test_it_is_tried_again_at_every_look_while_it_stays_missing);
    RUN_TEST(test_a_page_that_starts_its_own_task_is_not_given_a_second);
    RUN_TEST(test_leaving_the_page_calls_it_off);
    RUN_TEST(test_the_first_call_is_a_look);
    RUN_TEST(test_looks_survive_the_millisecond_counter_wrapping);
    return UNITY_END();
}
