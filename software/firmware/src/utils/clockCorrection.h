#ifndef LOCKBOX_CLOCK_CORRECTION_H
#define LOCKBOX_CLOCK_CORRECTION_H

#include <stdint.h>

struct ClockSnapshot {
    int64_t wallMs;
    int64_t monotonicMs;
};

static constexpr int64_t CLOCK_CORRECTION_THRESHOLD_MS = 5000;

int64_t currentWallClockMs();
int64_t currentMonotonicMs();
ClockSnapshot captureClockSnapshot();

// Returns false when the wall clock has not been set yet (year < 2000),
// which happens before the first NTP sync.
bool isWallClockSet();

void beginNtpClockCorrectionWindow();
bool recordNtpClockCorrection(const char* source);

// Stores the deadline of a local timer that starts now. True only once the
// deadline is in NVS: the caller must not lock on false (a zero or oversized
// duration, or a write that did not take).
bool saveLocalLockTimerDuration(uint64_t durationMs);
bool processPendingLocalTimerClockCorrections();
uint32_t localTimerCorrectionVersion();

void resetClockCorrectionForTest();

#endif  // LOCKBOX_CLOCK_CORRECTION_H
