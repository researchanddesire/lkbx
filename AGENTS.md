# AGENTS.md - Lockbox Firmware

## Ohai Assembly Docs Contract

This repository is the English-only lockbox spoke of the rad-app hub.
`assembly-docs/**` mirrors only
`rad-app/apps/docs-ohai/content/docs/lockbox/**`. Hub `page.en.mdx` becomes
`page.mdx` here, and `meta.en.json` becomes `meta.json`. Product assets,
KiCad sources and `_bom/` snapshots travel unchanged. Keep CSV, provenance and
generated JSON together; do not add translated pages or other products.

The notifier wakes rad-app after content changes. Reviewed sync PRs import to
hub staging, then fan out to the full mirror. Never sync directly to
assembly-docs or another spoke. `.github/assembly-sync.json` is the hub-managed
baseline; do not hand-edit it. Initial replacement of legacy MkDocs content
requires a separately reviewed hub seed PR. After seeding, use Fumadocs
`meta.json` and MDX; do not restore `site.yml`, `nav.yml`, or generated HTML
BOM markers. Hardware BOM source remains human-owned and outside content sync.

Normal synchronization is staging to staging; main follows reviewed
staging-to-main promotion. The notifier uses `RAD_VERSION_CONTROL_APP_ID` and
`RAD_VERSION_CONTROL_PRIVATE_KEY` with a token scoped only to rad-app.

Hardware source folders are `hardware/cad/`, `hardware/pcb/`, and
`hardware/cables/`. Do not commit generated assembly-site output, rendered BOM
HTML, `.DS_Store`, `cable-export/`, or generated cable render outputs.

Product BOMs list cable harnesses as top-level assemblies. Detailed cable BOMs
belong to Wireviz output; link generated `.bom.tsv` artifacts from assembly
docs instead of copying child cable BOM rows into `hardware/bom.csv`.

## Overview

Lockbox is an ESP32-S3 smart lock device firmware. It controls a DC motor lock mechanism with magnetic sensors, a TFT display, rotary encoder UI, and communicates with the RAD Dashboard via MQTT and HTTP. Built with PlatformIO, Arduino framework, and Boost.SML state machine.

## Tech Stack

- **MCU:** ESP32-S3-DevKitC-1-N16R8V
- **Framework:** Arduino + FreeRTOS
- **C++ Standard:** C++17 (gnu++17)
- **State Machine:** Boost.SML (header-only, `include/boost/sml.hpp`)
- **Display:** Adafruit ST7789 (320x240 TFT)
- **Motor:** DC motor with INA219 current sensing
- **Comms:** MQTT (esp_mqtt_client), WiFi (WiFiManager), BLE (NimBLE — always on at boot, deinited when MQTT TLS connects)
- **Build:** PlatformIO development/staging/production R2 and R8 profiles, plus native test profiles

## Quick Commands

```bash
pio run -e development-r8          # Use r2 for the verified R2 board
pio run -e staging-r2 -e staging-r8 # Build both staging artifacts
pio run -e production-r2 -e production-r8 # Compile both production profiles
pio run -e test -t test          # Run unit tests (native platform)
pio check                        # Run clang-tidy static analysis
```

## Project Structure

```
src/
├── main.cpp                 # Entry point, FreeRTOS task setup (WiFi, Button, Idle)
├── constants/               # Pin definitions, colors, sizes, config, menu structure
│   ├── Pins.h/cpp          # Hardware pin assignments (extern for calibration swap)
│   ├── Config.h            # Font and LED config
│   ├── Colors.h            # ST77xx display color definitions
│   ├── Sizes.h             # Display dimensions (320x240)
│   ├── Menu.h              # Menu item definitions
│   ├── PerferenceKeys.h    # NVS preference key namespaces
│   ├── Version.h           # Version info
│   └── copy/               # Localization strings (en-us.h, fr.h)
├── motor/
│   └── Motor.h/cpp         # DC motor lock/unlock with INA219 current monitoring
├── services/                # Hardware drivers and system services
│   ├── board.h             # Board init (pins, NTP, MQTT, WiFi)
│   ├── display.h/cpp       # ST7789 TFT display (mutex-protected)
│   ├── encoder.h           # Rotary encoder (AiEsp32RotaryEncoder)
│   ├── ina.h               # INA219 power monitor
│   ├── magnetics.h         # Hall effect + TMR sensors (paddle detection)
│   ├── mqtt.h/cpp          # MQTT client (esp_mqtt_client, QoS 2)
│   ├── nimble.h            # BLE server, service, characteristic, queue task
│   ├── ntp.h               # NTP time sync
│   ├── wm.h                # WiFi Manager (captive portal)
│   ├── machine.h           # State machine container
│   ├── settings.h          # NVS Preferences wrapper (mutex-protected)
│   ├── tasks.h             # FreeRTOS task handles
│   └── helpers/initMQTT.h  # MQTT event handler & init
├── state/                   # Boost.SML state machine (core logic)
│   ├── LKBX.h/cpp          # State machine definition (~62 states)
│   ├── LKBX.animation.cpp  # Animation states
│   ├── LKBX.BasicPages.cpp # Basic UI pages
│   ├── LKBX.break.cpp      # Break feature
│   ├── LKBX.menu.cpp       # Menu rendering
│   ├── LKBX.timer.cpp      # Timer countdown
│   ├── LKBX.wifi.cpp       # WiFi states
│   ├── Events.h            # Event structs (ButtonPress, Lock, Unlock, etc.)
│   ├── Actions.h           # Transition actions (lambdas)
│   └── Guard.h             # Transition guards (isLocked, isOnline, etc.)
├── structs/                 # Data structures (MenuItem, Action, Version, etc.)
├── ui/                      # Icon bitmaps and menu rendering
├── pages/                   # Text-based UI page definitions
└── utils/                   # Helpers (OTA update, formatting, BLE protocol, etc.)
    ├── StateLogger.h        # Boost.SML event logger
    ├── syncState.h          # Server state synchronization
    ├── update.h             # OTA firmware updates
    ├── RecusiveMutex.h      # Thread-safe recursive mutex wrapper
    └── dashboard.hpp        # Dashboard message parsing
assembly-docs/               # Self-contained OHAI-style assembly docs for future ohai.researchanddesire.com aggregation
hardware/pcb/                # KiCad PCB source, footprints, symbols, datasheets, STEP exports, and production fab files imported from Lockbox-Hardware
hardware/cad/                # Mechanical enclosure CAD landing zone
```

## Hardware Pin Map

| Function      | Pin(s)      | Notes                                    |
|---------------|-------------|------------------------------------------|
| Motor A       | 46          | Extern — can be swapped by calibration   |
| Motor B       | 45          | Direction control                        |
| Motor Mode    | 47          | Enable pin                               |
| Button Enter  | 12          | Extern — can be swapped by calibration   |
| Button Back   | 0           | Navigation                               |
| Power Detect  | 8           | USB power switch                         |
| TFT DC        | 7           | SPI data/command                         |
| TFT CS        | 1           | SPI chip select                          |
| TFT Reset     | 16          |                                          |
| TFT MOSI      | 5           | SPI data                                 |
| TFT SCLK      | 2           | SPI clock                                |
| TFT Backlight | 11          | PWM (LEDC channel 0)                     |
| Hall 1        | 6           | Backplate detection                      |
| Hall 2        | 15          | Backplate detection                      |
| TMR 1-3       | 17, 18, 21  | Paddle lock position (2 of 3 = locked)   |
| INA219 SDA    | 9           | I2C power sensor                         |
| INA219 SCL    | 10          | I2C power sensor                         |
| Encoder A/B   | 13, 14      | Rotary encoder                           |
| USB Detect    | 4           | High when USB powered                    |
| Accessory PWR | 48          | External accessory enable                |

## Key Patterns

### State Machine (Boost.SML)

The core logic lives in `src/state/LKBX.h`. The state machine has ~62 states with transitions defined in a table format:

```cpp
// Transition format: source_state + event [guard] / action = target_state
"home"_s + event<ButtonPress> [isCalibrated] / lockAction = "locking"_s
```

- **Thread Safety:** `sml::thread_safe<ESP32RecursiveMutex>`
- **Logging:** `StateLogger` traces all events, guards, actions, and state changes
- **Events:** `ButtonPress`, `BackPress`, `LongPress`, `Lock`, `Unlock`, `Break`, `ForceUpdate`, `WifiConnected`, `Done`, `Error`
- **Guards:** `isLocked()`, `isPaddleLocked()`, `isOnline()`, `isCalibrated()`, `isEmergencyBlocked()`

### FreeRTOS Tasks

3 tasks created in `main.cpp`:

| Task    | Core | Purpose                                      |
|---------|------|----------------------------------------------|
| WiFi    | 1    | Monitor/reconnect WiFi every 30s             |
| Button  | 0    | Process button inputs (25ms tick, max prio)  |
| Idle    | 0    | Power management (idle → sleep transitions)  |

### Idle/Sleep States

```
NOT_IDLE → (30s) → IDLE → (2min) → PSEUDO_SLEEP → (5min) → SLEEP (deep sleep)
```

Deep sleep uses ext0 wakeup on button press.

### Motor Calibration

On first boot, the motor calibration routine auto-detects:
- **Pin inversion:** Whether motor A/B need swapping
- **Button swap:** Whether enter button and motor pins are swapped
- Stored in NVS preferences: `isMotorInverted`, `isButtonInverted`

### Communication

**MQTT:**
- Subscribe: `<MAC_ADDRESS>` (receives commands)
- Publish: `<MAC_ADDRESS>` (state), `<MAC_ADDRESS>/log` (activity logs)
- QoS 2, TLS on port 8883 (prod), plain on 1883 (dev)
- Messages: "locked", "unlocked", "disconnected", "break", "emergency"

**HTTP (Server Sync):**
- `GET/POST {RAD_SERVER}/api/lockbox/state/{MAC_ADDRESS}`
- TLS in production (DigiCert root CA), insecure in dev

**WiFi:**
- WiFiManager captive portal: AP name "Lockbox Setup"
- Power saving: `WIFI_PS_MAX_MODEM`

### NVS Preferences

Persistent storage with mutex protection. Namespaces: `lockbox`, `session`, `mqtt`. Keys defined in `PerferenceKeys.h`.

### Error Codes

| Code      | Meaning                                 |
|-----------|-----------------------------------------|
| E-LOCK-1  | Could not lock                          |
| E-LOCK-2  | Could not unlock                        |
| E-LOCK-3  | No backplate detected                   |
| E-LOCK-10 | WiFi connection failed                  |
| E-LOCK-20 | Emergency unlock used (24hr lockout)    |

## Build Configuration

### Environments

| Profiles | Purpose | Hardware |
| --- | --- | --- |
| development-r2/r8 | Isolated local services | Verified R2/R8 |
| staging-r2/r8 | Staging dashboard; PR bench observation | Both R2 and R8 |
| production-r2/r8 | Production dashboard; compile on every PR | Both R2 and R8 |
| test, test_native | Native software checks | Host only |

### Build Script

`config/pre_build_script.py` runs before compilation:
- Replaces `localhost` with local machine IP in dev builds
- Sets `SW_VERSION` from environment

### Flash Partitions

Custom `config/partition.csv`: NVS (5KB), OTA data (2KB), App0/App1 (6.4MB each), SPIFFS (3.375MB), Coredump (64KB)

## Libraries

| Library                          | Purpose                    |
|----------------------------------|----------------------------|
| NimBLE-Arduino 1.4.2             | BLE stack                  |
| Adafruit GFX 1.11.5             | Graphics primitives        |
| Adafruit ST7735/ST7789 1.10.4   | TFT display driver         |
| Adafruit BusIO 1.14.1           | I2C/SPI abstraction        |
| Adafruit INA219 1.2.3           | Current sensor             |
| QRCode 0.0.1                    | QR code generation         |
| ArduinoJson 6.21.4              | JSON parsing               |
| Ai Esp32 Rotary Encoder 1.7     | Rotary encoder             |
| OneButton 2.6.1                 | Button debouncing          |
| WiFiManager (git)               | WiFi config portal         |
| ArduinoFake 0.4.0               | Test mocking (test only)   |

## Testing

- Framework: Unity (PlatformIO native)
- Mocking: ArduinoFake
- Test dirs: `test/test_measurements/`, `test/test_strings/`
- Run: `pio test -e test`

## Important Rules

- Never commit credentials or certificates
- Motor pins are extern and may be swapped by calibration — don't hardcode assumptions
- Display and preferences access must use their respective mutexes
- State machine is the single source of truth for device behavior — all logic flows through `LKBX.h`
- Emergency unlock triggers a 24-hour lockout period
- TMR sensor consensus: 2 of 3 sensors must agree for "paddle locked" status
- Current assembly docs use the newer build guide as source of truth: 4x 4mm magnets and T7 Torx driver.

## Hardware BOMs

When editing any `hardware/<product>/bom.csv`, follow the RAD BOM standard (fixed columns, closed category codes, `–` for empty cells). See `.cursor/rules/bom.mdc` for the inline contract and https://dev.researchanddesire.com/meta/bom-standard/ for the full reference. The vendored schema is `.github/workflows/scripts/bom.schema.json` (read-only); the `bom-lint` check enforces conformance.

## Workflow

- Never commit changes automatically. Let me review and commit manually.
- Be opinionated but explain reasoning.
- If you learn something new about this project, add it to this file.
- Always verify state machine transitions when modifying device behavior.

## Bounded Hardware Verification

- A request to reproduce, test, or repair the connected LKBX authorizes bounded BLE tests and installing the task firmware on that identified bench unit. Match the device identity before sending controls.
- Prefer camera evidence when available. If it is unavailable, proceed with BLE magnetic paddle/backplate sensors and available logs, without another confirmation. Preserve before/after evidence, bound the test duration, retain an unlock/stop cleanup path, and leave the unit unlocked unless the user requested a different final state.
- Report missing visual evidence. A sensor-based functional test does not satisfy a request to verify screen appearance or a release gate that explicitly requires visual evidence.

## PR hardware validation

- Every PR into staging or main builds all hardware variants, including docs-only changes.
- Install the exact staging bundle for the PR merge SHA; production still builds.
- Require Build validation and Hardware validation in addition to existing checks.
- Firmware bundles and hardware evidence are Actions artifacts, never tracked files.
- The reviewed harness and fixture inventory live outside the runner work directory.
- Never infer product identity or flash capacity from a COM port or advertising name.
- Follow scripts/hil/README.md for enrollment, shared-device locks, and rollout.
- Keep Wi-Fi credentials, fixture identities, and raw device logs outside the repository.
- A missing device or incomplete three-minute observation must fail validation.

## Development display capture

- Development builds expose the read-only USB `screen` command; staging and production omit it.
- Follow docs/development-screen-capture.md; use the display mutex for snapshots and keep serial transmission outside it.
- Preserve the capture mirror when adding TFT bulk drawing paths. Keep capture files outside Git.

## Serial identity

- All firmware tracks expose the read-only boot/`info` identity described in docs/serial-identity.md.
- Keep one serial input reader; development screen commands register with it.
- Report target memory and measured memory separately; a label never authorizes flashing or motion.
