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
python software/dev/dev_services.py configure --lan-ip <private-IP>   # local fixture services
cd software/firmware
pio run -e development-r8                                             # or development-r2
pio test -e test
pio check
```

## Repository layout

- `software/firmware/`: the PlatformIO project (`src/`, `include/`, `lib/`, `boards/`, `test/`, `platformio.ini`).
- `software/scripts/`: build-hook helpers, release, screen capture and rig tooling.
- `software/dev/`: local HTTPS and MQTT fixture services for development builds.

See [Architecture](architecture.md) for state machine and task structure. Full reference: [`AGENTS.md`](https://github.com/researchanddesire/Open-Source-Chastity-Lockbox_LKBX/blob/main/AGENTS.md) in the repository root.
