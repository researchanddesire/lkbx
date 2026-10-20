# Power Management

Firmware and electrical details of the Lockbox idle/sleep state machine, backlight control, and power consumption. For the user-facing overview, see [Sleep Mode](https://docs.researchanddesire.com/lockbox/device-states/sleep-mode/) and [Idle and Power States](https://docs.researchanddesire.com/lockbox/technical/idle-and-power-states/).

## Idle state progression

After any tracked interaction, the Idle task advances through states based on cumulative inactivity time:

| State | Cumulative timeout | Backlight PWM | WiFi | Est. current draw |
|-------|--------------------|---------------|------|-------------------|
| Active | — | 255 (100%) | Connected | ~80–100mA |
| Idle | 30s | 24 (~10%) | Connected | ~40–60mA |
| Pseudo-sleep | 2min | 0 (off) | Connected | ~20–30mA |
| Deep sleep | 10min | 0 (off) | Disconnected | ~0.5–2mA |

Timeouts are cumulative from the last interaction (deep sleep occurs 10 minutes after the last interaction, not 10 minutes after pseudo-sleep begins).

## Timeout constants

```cpp
IDLE_TIMEOUT         = 30000    // 30 seconds
PSEUDO_SLEEP_TIMEOUT = 120000   // 2 minutes
SLEEP_TIMEOUT        = 600000   // 10 minutes
```

!!! note "Reconcile with architecture.md"
    These constants give a 30s → 2min → 10min progression (deep sleep at `SLEEP_TIMEOUT = 600000` ms). The [Architecture](/lockbox/architecture/) page currently documents the idle chain as 30s → 2min → ~5min for deep sleep. The two should be reconciled against the firmware source; the constants above are authoritative.

## LED backlight PWM values

The display backlight is driven by PWM. Values per state:

```
Active:       255 (100%)
Idle:         24  (~10%)
Pseudo-sleep: 0   (off)
Deep sleep:   0   (off)
```

In Idle the screen remains powered and readable at the dimmed level. In pseudo-sleep the screen is still powered but the backlight is off.

## Pseudo-sleep specifics

- Backlight off (PWM 0); screen still powered
- WiFi remains connected so keyholder/dashboard commands still arrive
- Motor controller cleared
- Current sensor (INA219) enters power-save mode

When connected to USB power, pseudo-sleep is the deepest state entered—the device will not progress to deep sleep while charging.

## Deep sleep specifics

Deep sleep only activates when **all** conditions hold:

- Inactive for `SLEEP_TIMEOUT` (10 minutes)
- **Not** connected to USB power
- Not in a protected state (calibration, WiFi setup)

In deep sleep WiFi, Bluetooth, and most peripherals power down. Lock timers continue internally.

### Wake source

Wake from deep sleep is via ext0 button wakeup (Enter button, or Back button depending on hardware pin configuration). A timed lock nearing completion also forces the device to stay awake / wake to handle the unlock.

## Charging behavior

| Condition | Maximum sleep depth |
|-----------|---------------------|
| Charging (USB power) | Pseudo-sleep only |
| Not charging | Full deep sleep |

This keeps the device reachable for dashboard commands while charging and maximizes battery life on battery power.

## Timer-based wake

For timed locks the firmware computes an optimal wake duration so the device wakes from deep sleep before the lock timer expires:

```
sleepDuration = max(remainingTimeMs - PSEUDO_SLEEP_TIMEOUT, 0)
```

When a timed lock has less than `PSEUDO_SLEEP_TIMEOUT` remaining, the device stays awake to handle the unlock.

## Interaction tracking

Every tracked interaction resets the idle timer:

- Button presses (Enter, Back)
- Encoder rotation
- USB power state changes
- Sensor state changes (while actively monitoring)

## Related developer pages

- [Architecture](/lockbox/architecture/) — FreeRTOS Idle task and state machine
- [State and Sync](/lockbox/hardware/state-and-sync/) — NVS persistence and dashboard sync
