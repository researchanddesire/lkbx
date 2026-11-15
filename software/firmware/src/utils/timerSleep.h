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

// How long before its deadline a sleeping box wakes itself. A visible countdown
// wakes early, so its last minutes run on screen. A hidden timer wakes at the
// deadline itself: a box that lights up early says the end is near.
inline uint64_t timerWakeMarginMs(bool timerDisplayed, uint64_t wakeMargin) {
    return timerDisplayed ? wakeMargin : 0;
}

#endif
