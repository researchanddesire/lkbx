#include "unity.h"

#include "../../src/utils/lastWillPolicy.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr uint64_t kNow = 1789188000000ULL;  // 2026
}

// A box that holds a command key registers the sealed will, never a plain one:
// the dashboard refuses a plain will from it.
void test_keyed_box_seals_its_will() {
    TEST_ASSERT_TRUE(lastWillKind(true, false, kNow) == LastWill::SEALED);
    // The plain-report rule cannot turn a keyed box's will plain.
    TEST_ASSERT_TRUE(lastWillKind(true, true, kNow) == LastWill::SEALED);
}

// A sealed will stamped before 2024 is refused, so the box connects without
// one; the next connection's will works once the clock is set.
void test_keyed_box_with_an_unset_clock_registers_no_will() {
    TEST_ASSERT_TRUE(lastWillKind(true, false, 0) == LastWill::NONE);
    TEST_ASSERT_TRUE(lastWillKind(true, false, kLastWillEarliestMs - 1) ==
                     LastWill::NONE);
    TEST_ASSERT_TRUE(lastWillKind(true, false, kLastWillEarliestMs) ==
                     LastWill::SEALED);
}

// A box that has never signed keeps the plain will, clock or no clock: it
// carries no time of its own (createdAt is 0).
void test_box_without_a_key_keeps_the_plain_will() {
    TEST_ASSERT_TRUE(lastWillKind(false, true, kNow) == LastWill::PLAIN);
    TEST_ASSERT_TRUE(lastWillKind(false, true, 0) == LastWill::PLAIN);
}

// A box that signs but holds no key right now (a burned key before its first
// sign-in, or a key that has not loaded) never falls back to a plain will.
void test_box_that_signs_without_a_key_registers_no_will() {
    TEST_ASSERT_TRUE(lastWillKind(false, false, kNow) == LastWill::NONE);
    TEST_ASSERT_TRUE(lastWillKind(false, false, 0) == LastWill::NONE);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_keyed_box_seals_its_will);
    RUN_TEST(test_keyed_box_with_an_unset_clock_registers_no_will);
    RUN_TEST(test_box_without_a_key_keeps_the_plain_will);
    RUN_TEST(test_box_that_signs_without_a_key_registers_no_will);
    return UNITY_END();
}
