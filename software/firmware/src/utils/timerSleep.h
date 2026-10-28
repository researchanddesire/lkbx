#ifndef LOCKBOX_TIMER_SLEEP_H
#define LOCKBOX_TIMER_SLEEP_H

#include <cstdint>

// Zero allows indefinite sleep only when no timer is active. One keeps an
// expired or nearly expired timer awake, including when its deadline is absent.
inline uint64_t timerSleepDelayMs(bool active, uint64_t deadline,
                                  uint64_t now, uint64_t wakeMargin) {
    if (!active) return 0;
    if (deadline <= now || deadline - now <= wakeMargin) return 1;
    return deadline - now - wakeMargin;
}

#endif
