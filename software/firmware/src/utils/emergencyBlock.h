#ifndef LOCKBOX_EMERGENCY_BLOCK_H
#define LOCKBOX_EMERGENCY_BLOCK_H

#include <stdint.h>

// After a software emergency unlock the box refuses to lock again until the
// stored block end (24 h). The server reacts to the emergency report by
// archiving the session and publishing a clear, which reaches the box as an
// unlock event. That event must not end the block: only the clock may.
//
// A stored end of 0 (flag set by firmware that never wrote an end) counts as
// expired, matching the countdown task, so such a box is never stuck. An
// unsynced clock reads as the epoch, which is never past a real end.
inline bool emergencyBlockExpired(uint64_t nowMs, uint64_t blockEndMs) {
    return nowMs >= blockEndMs;
}

#endif  // LOCKBOX_EMERGENCY_BLOCK_H
