#ifndef LOCKBOX_MAGNETICS_H
#define LOCKBOX_MAGNETICS_H

void initMagnetics();

// Drains the magnetics ISR debounce and sends an activity log on
// the trailing edge. Must be called from a normal task, not an ISR.
void checkPendingSensorChange();

// Per-tick update of the debounced paddle state used by telemetry. Call once
// per activity-log tick (50ms) from a normal task, not an ISR.
void updateStablePaddleState();

// Debounced paddle-locked state for telemetry/activity-log reporting only.
// State-machine, motor, and UI logic continue to use the raw Guard.h reads.
bool getStablePaddleLocked();

#endif  // LOCKBOX_MAGNETICS_H
