#ifndef LOCKBOX_EMERGENCY_BLOCK_H
#define LOCKBOX_EMERGENCY_BLOCK_H

#include <stdint.h>

// After a software emergency unlock the box refuses to lock again until the
// stored block end (24 h). The server reacts to the emergency report by
// archiving the session and publishing a clear, which reaches the box as an
// unlock event; a dashboard or Chaster unlock and a new offer arrive the same
// way. None of them may end the block: only the clock does.
//
// A stored end of 0 (a flag written by firmware that kept no end) counts as
// expired, so such a box is never stranded. The caller passes the box's wall
// clock. Before the first verified time sync that clock runs on from the RTC
// after a warm reboot and starts near the epoch after a cold one; both read
// as "not yet", the same way the countdown task sees them.
inline bool emergencyBlockExpired(uint64_t nowMs, uint64_t blockEndMs) {
    return nowMs >= blockEndMs;
}

#endif  // LOCKBOX_EMERGENCY_BLOCK_H
