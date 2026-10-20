# Architecture

## State machine

Core logic is a Boost.SML state machine in `src/state/LKBX.h` (~62 states). Events include `ButtonPress`, `Lock`, `Unlock`, `Break`, `WifiConnected`, `Done`, `Error`. Guards cover lock status, paddle position, online state, calibration, and emergency lockout.

Logging via `StateLogger` traces events, guards, actions, and transitions.

## FreeRTOS tasks

| Task | Core | Role |
|------|------|------|
| WiFi | 1 | Monitor/reconnect every 30s |
| Button | 0 | Button input 25ms tick |
| Idle | 0 | Idle → pseudo-sleep → deep sleep |

## Idle / sleep

`NOT_IDLE` → (30s) → `IDLE` → (2min) → `PSEUDO_SLEEP` → (5min) → deep sleep (ext0 wakeup on button).

## Motor calibration

First boot auto-detects motor pin inversion and button swap; stored in NVS (`isMotorInverted`, `isButtonInverted`).

## Communication

- **MQTT:** subscribe/publish on device MAC, QoS 2, TLS in production
- **HTTP:** `{RAD_SERVER}/api/lockbox/...` for state sync
- **BLE:** NimBLE until MQTT TLS connects

Configure `RAD_SERVER` and `MQTT_SERVER` in `platformio_private.ini`.
