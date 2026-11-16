#include <unity.h>

#include "../../src/utils/stationKeeper.h"

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t kWait = stationKeeper::kDownBeforeBeginMs;
}  // namespace

void test_a_connected_station_is_left_alone() {
    StationKeeper keeper;
    TEST_ASSERT_FALSE(keeper.due(true, false, 0));
    TEST_ASSERT_FALSE(keeper.due(true, false, 10 * kWait));
}

// A drop is usually mended by the Wi-Fi driver within seconds. The box only
// steps in when the station has stayed down.
void test_a_station_down_for_a_moment_is_left_to_reconnect_by_itself() {
    StationKeeper keeper;
    TEST_ASSERT_FALSE(keeper.due(false, false, 1000));
    TEST_ASSERT_FALSE(keeper.due(false, false, 1000 + kWait - 1));
    TEST_ASSERT_FALSE(keeper.due(true, false, 1000 + kWait));
    // A later drop gets the full wait again.
    TEST_ASSERT_FALSE(keeper.due(false, false, 5000 + kWait));
    TEST_ASSERT_FALSE(keeper.due(false, false, 5000 + 2 * kWait - 1));
}

// The fault: R8 stayed without network for over four minutes, until it was
// restarted.
void test_a_station_that_stays_down_is_begun_again_and_again() {
    StationKeeper keeper;
    uint32_t now = 2000;
    TEST_ASSERT_FALSE(keeper.due(false, false, now));
    for (int attempt = 0; attempt < 10; ++attempt) {
        TEST_ASSERT_FALSE(keeper.due(false, false, now + kWait - 1));
        now += kWait;
        TEST_ASSERT_TRUE(keeper.due(false, false, now));
        // Once per interval, however often it is asked.
        TEST_ASSERT_FALSE(keeper.due(false, false, now));
        TEST_ASSERT_FALSE(keeper.due(false, false, now + 50));
    }
    TEST_ASSERT_FALSE(keeper.due(true, false, now + 100));
}

// A station that scans takes the setup portal's access point off its
// channel, so the station is never begun while a portal is open, and the
// wait starts again when the portal has closed.
void test_the_station_is_left_alone_while_the_portal_is_open() {
    StationKeeper keeper;
    TEST_ASSERT_FALSE(keeper.due(false, false, 0));
    TEST_ASSERT_FALSE(keeper.due(false, true, kWait));
    TEST_ASSERT_FALSE(keeper.due(false, true, 20 * kWait));
    // Closed with the station still down, as WiFiManager can leave it.
    const uint32_t closed = 20 * kWait + 500;
    TEST_ASSERT_FALSE(keeper.due(false, false, closed));
    TEST_ASSERT_FALSE(keeper.due(false, false, closed + kWait - 1));
    TEST_ASSERT_TRUE(keeper.due(false, false, closed + kWait));
}

void test_waits_survive_the_millisecond_counter_wrapping() {
    StationKeeper keeper;
    const uint32_t droppedAt = UINT32_MAX - 4000;
    TEST_ASSERT_FALSE(keeper.due(false, false, droppedAt));
    TEST_ASSERT_FALSE(keeper.due(false, false, droppedAt + kWait - 1));
    TEST_ASSERT_TRUE(keeper.due(false, false, droppedAt + kWait));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_a_connected_station_is_left_alone);
    RUN_TEST(test_a_station_down_for_a_moment_is_left_to_reconnect_by_itself);
    RUN_TEST(test_a_station_that_stays_down_is_begun_again_and_again);
    RUN_TEST(test_the_station_is_left_alone_while_the_portal_is_open);
    RUN_TEST(test_waits_survive_the_millisecond_counter_wrapping);
    return UNITY_END();
}
