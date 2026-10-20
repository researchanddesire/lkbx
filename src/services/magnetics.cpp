#include "magnetics.h"

#include <Arduino.h>

#include "../constants/Pins.h"
#include "lastInteraction.h"
#include "machine.h"

namespace {

constexpr unsigned long SENSOR_DEBOUNCE_MS = 50;

// Number of consecutive 50ms ticks a *changed* paddle reading must hold before
// it is reported in telemetry. ~300ms: long enough to reject the magnetic
// transient seen when the backplate is moved slowly (its magnet lingers near
// the TMR sensors and briefly trips them), far shorter than a real motor-driven
// lock/unlock.
constexpr int PADDLE_STABLE_TICKS = 6;

volatile unsigned long lastSensorChangeTime = 0;

// Telemetry-only debounced paddle state. The state machine, motor, and UI keep
// reading the raw guard (Guard.h::isPaddleLocked) directly and are unaffected;
// only the activity-log payload reads getStablePaddleLocked().
bool stablePaddleLocked = false;
bool candidatePaddleLocked = false;
int paddleStableCount = 0;
bool paddleDebounceInited = false;

// Mirrors Guard.h::isPaddleLocked (2-of-3 TMR consensus). Read locally to keep
// this hardware module self-contained — Guard.h pulls in the state-machine
// headers, which we don't want as a dependency of the magnetics service.
bool rawPaddleLocked() {
    bool tmr1 = digitalRead(Pins::Magnetics::tmr1) == LOW;
    bool tmr2 = digitalRead(Pins::Magnetics::tmr2) == LOW;
    bool tmr3 = digitalRead(Pins::Magnetics::tmr3) == LOW;
    return (tmr1 && tmr2 && tmr3) || (tmr1 && tmr2) || (tmr1 && tmr3) ||
           (tmr2 && tmr3);
}

void IRAM_ATTR handleSensorInterrupt() {
    setNotIdleISR();
    lastSensorChangeTime = millis();
}

}  // namespace

void initMagnetics() {
    pinMode(Pins::Magnetics::hall1, INPUT_PULLUP);
    pinMode(Pins::Magnetics::hall2, INPUT_PULLUP);
    pinMode(Pins::Magnetics::tmr1, INPUT_PULLUP);
    pinMode(Pins::Magnetics::tmr2, INPUT_PULLUP);
    pinMode(Pins::Magnetics::tmr3, INPUT_PULLUP);

    // CHANGE (not FALLING): a magnet leaving a sensor (backplate opened, paddle
    // released) is a rising edge. FALLING-only missed those releases, so the
    // dashboard kept reporting stale "closed"/"locked" until some unrelated
    // event happened to fire a publish. CHANGE captures both engage and release;
    // the 50ms trailing debounce in checkPendingSensorChange() bounds the rate.
    attachInterrupt(digitalPinToInterrupt(Pins::Magnetics::hall1),
                    handleSensorInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(Pins::Magnetics::hall2),
                    handleSensorInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(Pins::Magnetics::tmr1),
                    handleSensorInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(Pins::Magnetics::tmr2),
                    handleSensorInterrupt, CHANGE);
    attachInterrupt(digitalPinToInterrupt(Pins::Magnetics::tmr3),
                    handleSensorInterrupt, CHANGE);

    // Seed the debounced paddle state from the current reading so the first
    // telemetry report is accurate rather than a default.
    stablePaddleLocked = rawPaddleLocked();
    candidatePaddleLocked = stablePaddleLocked;
    paddleStableCount = 0;
    paddleDebounceInited = true;
}

void checkPendingSensorChange() {
    static unsigned long debounceTimer = 0;
    static bool timerActive = false;

    unsigned long currentTime = millis();

    if (lastSensorChangeTime > 0) {
        debounceTimer = currentTime;
        timerActive = true;
        lastSensorChangeTime = 0;
    }

    if (timerActive && currentTime - debounceTimer >= SENSOR_DEBOUNCE_MS) {
        if (lkbx) {
            lkbx->sendActivityLog("sensor_change");
        }
        timerActive = false;
    }
}

void updateStablePaddleState() {
    bool raw = rawPaddleLocked();

    if (!paddleDebounceInited) {
        stablePaddleLocked = raw;
        candidatePaddleLocked = raw;
        paddleStableCount = 0;
        paddleDebounceInited = true;
        return;
    }

    // No change versus the reported value: reset the candidate tracking.
    if (raw == stablePaddleLocked) {
        candidatePaddleLocked = stablePaddleLocked;
        paddleStableCount = 0;
        return;
    }

    // A different reading: count how long it holds before committing it.
    if (raw == candidatePaddleLocked) {
        paddleStableCount++;
    } else {
        candidatePaddleLocked = raw;
        paddleStableCount = 1;
    }

    if (paddleStableCount >= PADDLE_STABLE_TICKS) {
        stablePaddleLocked = candidatePaddleLocked;
        paddleStableCount = 0;
        // The raw sensor event is published after 50ms, before this 300ms
        // debounce settles. Publish the accepted value too so Dashboard does
        // not retain the previous paddle state until another event occurs.
        if (lkbx) {
            lkbx->sendActivityLog("sensor_change");
        }
    }
}

bool getStablePaddleLocked() {
    return stablePaddleLocked;
}
