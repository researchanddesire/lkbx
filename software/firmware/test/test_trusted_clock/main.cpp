#include <unity.h>
#include <cstdint>
#include <sys/time.h>

void setUp() {}
void tearDown() {}

int64_t mockMonotonicMs = 0;
int64_t mockSystemMs = 0;
static int fakeGetTimeOfDay(timeval* tv, void*) {
    tv->tv_sec = mockSystemMs / 1000;
    tv->tv_usec = (mockSystemMs % 1000) * 1000;
    return 0;
}
static int fakeSetTimeOfDay(const timeval* tv, const void*) {
    mockSystemMs = int64_t(tv->tv_sec) * 1000 + tv->tv_usec / 1000;
    return 0;
}
#define gettimeofday fakeGetTimeOfDay
#define settimeofday fakeSetTimeOfDay
#include "../../src/utils/trustedClock.cpp"
#undef gettimeofday
#undef settimeofday

int64_t applicationMs() {
    timeval tv{};
    lockboxGetTimeOfDay(&tv);
    return int64_t(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

bool tryCompetingTimeCorrection(int64_t* correctionSnapshot);

void unauthenticated_sntp_does_not_change_clock() {
    clearNetworkTimeHint();
    mockSystemMs = 1788800000000;
    const timeval untrusted{2000000000, 500000};
    auto hint = untrusted;
    sntp_sync_time(&hint);
    TEST_ASSERT_EQUAL_INT64(1788800000000, applicationMs());
    TEST_ASSERT_EQUAL_UINT64(2000000000500, networkTimeHintMs());
    hint.tv_sec = 1;
    sntp_sync_time(&hint);
    TEST_ASSERT_EQUAL_UINT64(2000000000500, networkTimeHintMs());
}

void failed_https_restores_clock_without_exposing_hint() {
    mockSystemMs = 1788800000000;
    mockMonotonicMs = 100;
    TEST_ASSERT_TRUE(beginTlsClockBootstrap(2000000000000));
    TEST_ASSERT_EQUAL_INT64(2000000000000, mockSystemMs);
    mockMonotonicMs = 200;
    TEST_ASSERT_EQUAL_INT64(1788800000100, applicationMs());
    TEST_ASSERT_FALSE(beginTlsClockBootstrap(2000000000000));
    timeval rejected{2000000000, 0};
    TEST_ASSERT_EQUAL(-1, lockboxSetTimeOfDay(&rejected));
    TEST_ASSERT_FALSE(finishTlsClockBootstrap(0));
    TEST_ASSERT_EQUAL_INT64(1788800000100, mockSystemMs);
    TEST_ASSERT_EQUAL_INT64(1788800000100, applicationMs());
}

void authenticated_time_is_applied_once_and_late_sntp_is_only_a_hint() {
    mockSystemMs = 1788800000000;
    TEST_ASSERT_TRUE(beginTlsClockBootstrap(2000000000000));
    TEST_ASSERT_TRUE(finishTlsClockBootstrap(1788900000000));
    TEST_ASSERT_EQUAL_INT64(1788900000000, applicationMs());
    timeval later{2000000000, 0};
    sntp_sync_time(&later);
    TEST_ASSERT_EQUAL_INT64(1788900000000, applicationMs());
    TEST_ASSERT_FALSE(beginTlsClockBootstrap(UINT64_MAX));
    TEST_ASSERT_EQUAL_INT64(1788900000000, applicationMs());
    TEST_ASSERT_EQUAL(1788900000, lockboxTime());
}

void concurrent_bootstrap_cannot_capture_another_callers_hint() {
    mockSystemMs = 1788800000000;
    mockMonotonicMs = 100;
    TEST_ASSERT_TRUE(beginTlsClockBootstrap(2000000000000));
    bool began = true;
    bool finished = true;
    std::thread concurrent([&] {
        began = beginTlsClockBootstrap(2000000000000);
        finished = finishTlsClockBootstrap(2000000000000);
    });
    concurrent.join();
    TEST_ASSERT_FALSE(began);
    TEST_ASSERT_FALSE(finished);
    TEST_ASSERT_EQUAL_INT64(1788800000000, applicationMs());
    TEST_ASSERT_FALSE(finishTlsClockBootstrap(0));
    TEST_ASSERT_EQUAL_INT64(1788800000000, applicationMs());
}

void correction_snapshot_survives_a_competing_sync_after_tls_finishes() {
    constexpr int64_t oldTime = 1704067200000;
    constexpr int64_t verifiedTime = 1788800000000;
    constexpr int64_t duration = 60000;
    mockSystemMs = oldTime;
    mockMonotonicMs = 100;
    int64_t correctionSnapshot = applicationMs();
    int64_t localDeadline = oldTime + duration;
    {
        const NetworkTimeTransaction transaction;
        TEST_ASSERT_TRUE(bool(transaction));
        TEST_ASSERT_TRUE(beginTlsClockBootstrap(2000000000000));
        TEST_ASSERT_TRUE(finishTlsClockBootstrap(verifiedTime));
        // Critical interleaving: the TLS mutex is released, but the original
        // caller has not yet recorded its wall-clock jump for local timers.
        bool competed = true;
        std::thread contender([&] {
            competed = tryCompetingTimeCorrection(&correctionSnapshot);
        });
        contender.join();
        TEST_ASSERT_FALSE(competed);
        TEST_ASSERT_EQUAL_INT64(oldTime, correctionSnapshot);
        localDeadline += applicationMs() - correctionSnapshot;
        TEST_ASSERT_EQUAL_INT64(duration, localDeadline - applicationMs());
    }
    TEST_ASSERT_TRUE(tryCompetingTimeCorrection(&correctionSnapshot));
    TEST_ASSERT_EQUAL_INT64(verifiedTime, correctionSnapshot);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(unauthenticated_sntp_does_not_change_clock);
    RUN_TEST(failed_https_restores_clock_without_exposing_hint);
    RUN_TEST(authenticated_time_is_applied_once_and_late_sntp_is_only_a_hint);
    RUN_TEST(concurrent_bootstrap_cannot_capture_another_callers_hint);
    RUN_TEST(correction_snapshot_survives_a_competing_sync_after_tls_finishes);
    return UNITY_END();
}
