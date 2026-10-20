# AGENTS.md - Chastity Lockbox (LKBX)

## Repository

- `researchanddesire/Open-Source-Chastity-Lockbox_LKBX` is the canonical LKBX firmware, hardware and assembly-docs repository. Its history restarts at a single 2026-10-20 import of firmware 1.21.62; never rewrite `main` or `staging` history again.
- `researchanddesire/__legacy_Lockbox` (formerly `Lockbox`) is retired: its Actions and GitHub Pages are off. Do not land work there.
- Use `AJ <ajay.koenig@gmail.com>` for commits. GitHub-facing text never mentions AI agents or tools.

## Branch and Release Policy

- Persistent branches are `staging` and `main`. Fetch first, branch feature work from `origin/staging` with an `AJ/` prefix, and open the pull request back into `staging`.
- Promote with a same-repository `staging` → `main` pull request. Only urgent production fixes target `main` directly: branch from `origin/main` and start the PR title with the standalone word `hotfix` (`PR target policy` enforces this). Merge each hotfix back into `staging` directly, without a PR.
- Squash feature and hotfix PRs. Merge `staging` → `main` promotions with a merge commit, and merge any PR that introduces a reviewed `hardware-run.yml` wrapper with a merge commit so its pinned SHA stays reachable.
- Staging firmware reports the `staging` track and uses `https://staging.researchanddesire.com`; main firmware reports `main` and uses `https://dashboard.researchanddesire.com`.

## Firmware Releases

- Releases go only to the R+D firmware control plane (`/api/internal/firmware/v1`) and Supabase Storage bucket `lkbx-firmware`. Devices and the RAD app update only through `/api/firmware/v1/check` and the web-flasher catalog. Never publish firmware through GitHub Pages, `docs/`, or committed binaries.
- `publish_firmware.yml` runs on firmware-input pushes to `staging`/`main`: it allocates the next version, commits `AUTO: Version bump <track> <version>` as `rad-version-control[bot]`, builds R2 and R8, and publishes immutable *validating* candidates with signed provenance. Do not hand-edit `src/constants/Version.h` to release.
- `finalize_release.yml` (manual, production) tags a main release only after the control plane reports both variants ready with build, unit, integration, artifact and hardware validations passed. Rollout stays a control-plane action.
- Environments: `firmware-staging` (branch `staging`) and `firmware-production` (branch `main`), each holding `FIRMWARE_PUBLISH_TOKEN` and `FIRMWARE_PROVENANCE_SIGNING_KEY_PEM`. The `rad-version-control` app credentials are organization-level. Never print, log or commit secret values.

## Authorized Supabase Projects

- Production/main project ref: `acjajruwevyyatztbkdf`.
- Staging project ref: `meuaxbjzqrszxdvmacug`.
- Every remote Supabase-affecting firmware operation must explicitly target the project ref matching its track. Never infer a project from a URL, environment variable, cached CLI link, neighboring repository or prior conversation.

## Documentation (rad-app hub spoke)

- `assembly-docs/**` on `main` is authoritative for the LKBX assembly site. The rad-app hub imports this spoke's current `main` head (product `lockbox`, path `assembly-docs`); it keeps no spoke SHAs or baselines. `trigger-assembly-docs.yml` notifies the hub on `main` pushes only.
- Documentation changes land on `main` only and reach `staging` through the main → staging back-merge; `main` wins docs conflicts. Never sync or reconcile docs on `staging`.
- Every `assembly-docs` change on `main`, including a deletion, reaches the live site within minutes without review. Ask AJ before any change that could remove written documentation.
- `developer-docs/**` feeds dev.researchanddesire.com through `trigger-dev-docs.yml`.
- Keep assembly docs as English Fumadocs MDX with `meta.json`; keep `_bom/` CSV, provenance and generated JSON together. Hardware sources live in `hardware/cad/`, `hardware/pcb/` and `hardware/cables/`. Do not commit generated assembly-site output, rendered BOM HTML, `.DS_Store`, `cable-export/` or cable render outputs.
- Product BOMs list cable harnesses as top-level assemblies; link Wireviz `.bom.tsv` output instead of copying child cable rows into `hardware/bom.csv`.

## Technician Build PC

- Use the `$build-pc` skill for the Windows workstation technicians use to build and flash devices for sale. Keep its checkout on a clean, current `main`; preserve unexpected work before synchronizing.
- Repair build-machine tooling there, but fix tracked code or configuration in this repository through the normal branch workflow.

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

### Build Scripts

- `pre_build_script.py` applies `scripts/patch_sdk_security.py` to every device profile and, for development profiles only, force-includes the generated `.dev/local/include/lkbx_dev_config.h` from `scripts/dev_services.py`. Development builds never contact cloud services.
- `firmware_build_metadata.py` (with `serial_identity_build.py`) stamps firmware provenance and serial identity.

### Flash Partitions

`partition.csv`: NVS (5KB), OTA data (2KB), App0/App1 (6.4MB each), SPIFFS (3.375MB), Coredump (64KB)

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

- Native firmware: `pio test -e test` and `pio test -e test_native` (Unity + ArduinoFake; suites under `test/`).
- Tooling: `python -m unittest discover -s scripts -p 'test_*.py'`, `python -m unittest discover -s scripts/hil -p 'test_*.py'`, and `python -m unittest discover -s .github/scripts -p 'test_*.py'`.
- Sanitizer suites: `python scripts/test_http_worker_security.py` and `python scripts/test_firmware_runtime.py`.
- Secret/boundary scan: `python scripts/check_software.py` (needs gitleaks).

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

- Commit and publish completed, verified work through the branch policy above; stage only task files.
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
- The shared rig (`DESKTOP-SGS6GOQ`, `C:\rad-hil`, runner `lkbx-rig-01`) serves every firmware product. `scripts/hil/catalog.py` and `reviewed-workflows.json` are identical across LKBX, DTT, OSSM and RADR; change them in all four together, reinstall with `install.ps1`, and update the organization runner group `rad-firmware-validation` allowlist to the exact reviewed `hardware-run.yml@<sha>` refs.
- `pr-firmware.yml` calls the reviewed wrapper by full SHA. Keep that commit reachable (branch `AJ/reviewed-hardware-runner`).
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
