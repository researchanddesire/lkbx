# Lockbox — Developer Documentation

Lockbox is ESP32-S3 firmware for a smart chastity lock: DC motor, magnetic sensors, TFT + rotary encoder UI, MQTT/HTTP dashboard sync, BLE.

Published docs are aggregated at [dev.researchanddesire.com/lockbox](https://dev.researchanddesire.com/lockbox/). **Source of truth is this folder** in the Lockbox repo — edit here, not in the dev-docs aggregate repo.

## Tech stack

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3-DevKitC-1-N16R8V |
| Framework | Arduino + FreeRTOS |
| C++ | gnu++17 |
| State machine | Boost.SML (`include/boost/sml.hpp`) |
| Display | Adafruit ST7789 320×240 |
| Motor | DC + INA219 current sense |
| Comms | MQTT, WiFi (WiFiManager), NimBLE |

## Build

```bash
cp platformio_private.ini.example platformio_private.ini   # edit endpoints
pio run -e development
pio run -e test -t test
pio check
```

## Repository layout

See [Architecture](architecture.md) for state machine and task structure. Full reference: [`CLAUDE.md`](https://github.com/researchanddesire/Lockbox/blob/main/CLAUDE.md) in repo root (AI/local contributor context).
