# AGENTS.md - Chastity Lockbox (LKBX)

## Repository

- `researchanddesire/Open-Source-Chastity-Lockbox_LKBX` is the canonical LKBX firmware, hardware and assembly-docs repository. Its history restarts at a single 2026-10-20 import of firmware 1.21.62; never rewrite `main` or `staging` history again.
- `researchanddesire/__legacy_Lockbox` (formerly `Lockbox`) is retired: its Actions and GitHub Pages are off. Do not land work there.
- Use `AJ <ajay.koenig@gmail.com>` for commits. GitHub-facing text never mentions AI agents or tools.

## Branch and Release Policy

- Persistent branches are lowercase `staging` and `main`. Fetch before branching.
- The firmware directory is the PlatformIO project in `software/firmware/` (`src/`, `include/`, `lib/`, `boards/`, `test/`, `platformio.ini`, `partition.csv`, `sdkconfig*`, `CMakeLists.txt` and the build hooks) plus `software/scripts/patch_sdk_security.py`, which every device build applies. `FIRMWARE_PATHS` in `pr-firmware.yml` and `pr-target-policy.yml` and the `publish_firmware.yml` push paths define it; keep them in sync.
- Firmware changes enter `staging` first: branch from `origin/staging` and open the PR into `staging`. Promote with a same-repository `staging` → `main` PR.
- A production fix may take firmware to `main` directly from a `fix/<issue>` branch (an owner prefix such as `AJ/fix/<issue>` also works) started from `origin/main`.
- Changes outside the firmware directory (documentation, hardware, BOMs, workflows) may open their PR straight into `main`; they need no build or hardware checks. `PR target policy` enforces these rules.
- On every push to `main` and once a day, `hotfix-sync.yml` merges `main` back into `staging` directly as `rad-version-control[bot]` (main wins documentation and the firmware version). It opens a non-draft `main` → `staging` PR only when staging changed the same code.
- Firmware versions are allocated only for merges into `main`: `publish_firmware.yml` runs `.github/scripts/allocate_version.sh` (identical in every firmware repository), commits `AUTO: Version bump main <version>` to `Version.h` and `version.json` as `rad-version-control[bot]`, then builds, signs and publishes the candidate. Staging merges allocate nothing. Never hand-edit the version.
- `rad-version-control[bot]` bypasses every branch ruleset so version bumps and syncs push directly. Never open draft PRs.
- Squash feature and fix PRs into one focused commit with a concise, imperative title. Merge `staging` → `main` promotions and `main` → `staging` sync PRs with a merge commit; never squash or rebase them.
- Staging firmware reports the `staging` track and uses `https://staging.researchanddesire.com`; main firmware reports `main` and uses `https://dashboard.researchanddesire.com`.

## Firmware Releases

- Releases go only to the R+D firmware control plane (`/api/internal/firmware/v1`) and Supabase Storage bucket `lkbx-firmware`. Devices and the RAD app update only through `/api/firmware/v1/check` and the web-flasher catalog. Never publish firmware through GitHub Pages, `docs/`, or committed binaries.
- `publish_firmware.yml` runs on firmware-input pushes to `main`: it allocates the next version, commits `AUTO: Version bump main <version>` as `rad-version-control[bot]`, builds R2 and R8, and publishes immutable *validating* candidates with signed provenance. Do not hand-edit `software/firmware/src/constants/Version.h` to release.
- `finalize_release.yml` (manual, production) tags a main release only after the control plane reports both variants ready with build, unit, integration, artifact and hardware validations passed. Rollout stays a control-plane action.
- Environments: `firmware-staging` (branch `staging`) and `firmware-production` (branch `main`), each holding `FIRMWARE_PUBLISH_TOKEN` and `FIRMWARE_PROVENANCE_SIGNING_KEY_PEM`. The `rad-version-control` app credentials are organization-level. Never print, log or commit secret values.

## Supabase

- The only Supabase project agents and workflows address is `acjajruwevyyatztbkdf`, through the organization secret `SUPABASE_ACCESS_TOKEN`. Name it explicitly in every remote operation; never infer a project from a URL, environment variable, cached CLI link, neighboring repository or prior conversation.
- Firmware releases reach it only through the dashboard firmware API. PR hardware evidence videos go to its private `hil-evidence` bucket.

## Documentation (rad-app hub spoke)

- `assembly-docs/**` on `main` is authoritative for the LKBX assembly site. The rad-app hub imports this spoke's current `main` head (product `lockbox`, path `assembly-docs`); it keeps no spoke SHAs or baselines. `trigger-assembly-docs.yml` notifies the hub on `main` pushes only.
- Documentation changes land on `main` only and reach `staging` through the main → staging back-merge; `main` wins docs conflicts. Never sync or reconcile docs on `staging`.
- Every `assembly-docs` change on `main`, including a deletion, reaches the live site within minutes without review. Ask AJ before any change that could remove written documentation.
- Keep `assembly-docs/_bom/source.json` at `{"repo": "Open-Source-Chastity-Lockbox_LKBX", "commit": "main"}`. Whenever `_bom/bom.csv` changes, regenerate `_bom/bom.json` (same repo/commit, columns and rows parsed from the CSV) in the same commit, or the hub import fails.
- Never rewrite `main` history, and keep `trigger-assembly-docs.yml` on `main`.
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
cd software/firmware
pio run -e development-r8          # Use r2 for the verified R2 board
pio run -e staging-r2 -e staging-r8 # Build both staging artifacts
pio run -e production-r2 -e production-r8 # Compile both production profiles
pio run -e test -t test          # Run unit tests (native platform)
pio check                        # Run clang-tidy static analysis
```

## Project Structure

All software lives in `software/`:

```
software/
├── firmware/   # PlatformIO project; run `pio` here
├── scripts/    # build-hook helpers, release, screen capture and rig (hil/) tooling
└── dev/        # local services: compose.yaml, dev_services.py, fixture/ (Mosquitto fixtures or local RAD App front)
```

Firmware sources in `software/firmware/`:

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
```

Repository root: `assembly-docs/` (self-contained OHAI-style assembly docs), `hardware/pcb/` (KiCad PCB source, footprints, symbols, datasheets, STEP exports and production fab files), and `hardware/cad/` (mechanical enclosure CAD).

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
- Subscribe: `<MAC_ADDRESS>` (receives commands); after a v2 sign-in `v2/<deviceId>` instead (`cloudTopic()`)
- Publish: `<MAC_ADDRESS>` (state), `<MAC_ADDRESS>/log` (activity logs), or the same under `v2/<deviceId>`
- QoS 2, TLS on port 8883 (prod), plain on 1883 (dev)
- Messages: "locked", "unlocked", "disconnected", "break", "emergency"
- Commands are `sync` and `reboot` only. Never re-add a command that moves the motor directly.
- A `sync` with `clear: true` unlocks only when its `sessionId` is the session the device holds (`utils/serverClear.h`); a clear for another session is ignored. A clear without a `sessionId` comes from a dashboard older than rad-app's 2026-11-04 fix: the device pulls its state over HTTPS (`confirmServerClear`) and unlocks only if the dashboard reports no active session. The HTTPS state route's own `clear` stays authoritative.
- While the box holds a locked session (`locked`, `break` or `break_ending`), an MQTT `sync` that names any other session, or none, is never applied (`utils/heldSession.h`): the device pulls its state over HTTPS instead and applies that answer. A second session can therefore never move the paddle by MQTT; the HTTPS answer, the one session the dashboard considers active, still replaces a session that ended while the box was offline. Re-offers of the held session stay no-ops.

**Device identity (`software/firmware/lib/LockboxSecurity/README.md`):**
- `services/deviceIdentity` owns the device secret and command key (NVS namespace `identity`). Never log either value. The secret goes only to the configured RAD server, and is withheld while the running OTA image is pending verification.
- Firmware burns exactly one eFuse key block, once: the v2 device key the dashboard issues through `POST /api/lockbox/v2/provision` (purpose HMAC_UP, read- and write-protected, highest free key block). `burnDeviceKey` in `services/deviceIdentity` is the only firmware burn path, and `services/deviceKeyProvisioning` runs every refusing check (image confirmed, no key yet, free block, USB or battery >= 3.5 V) before it asks for a key. Development builds burn only simulated eFuses (`development-r8-virtual-efuse`) unless built with `LKBX_ALLOW_REAL_EFUSE_BURN`. Never log the key, the token or the v1 secret.
- `software/scripts/provision_device_identity.py` stays a manual factory tool, and rig or CI automation must never run it. A unit it provisioned holds a key the dashboard does not know, so it never signs in with v2; do not use it on units that will run 2.x firmware.
- With a burned key the device signs in only through `/api/lockbox/v2/*` with its token (HMAC of `lkbx-device-token-v2`) and never presents its MAC as proof. Only a 404 or a 401 `unknown_device` from v2 falls back to the v1 routes for that boot; an outage, a 429 or any other refusal retries v2, so it can never switch signing off. A v2 200 must carry `commandKey` and this chip's `deviceId`. Keep the shared vectors in `test/test_device_key` identical to rad-app's `v2Vector.spec.ts`.
- After a v2 sign-in the box is named by its device id, never its MAC: lowercase hex of the first 16 bytes of HMAC(key, `lkbx-device-id-v2`). Topics become `v2/<deviceId>`, commands v3 envelopes bound to the id, reports v3 seals. A v1 sign-in keeps the MAC topics and v2 formats. The id is an address, not proof.
- A device holding a command key accepts only verified envelopes (`lib/LockboxSecurity/src/CommandEnvelope.h`): v2 bound to the MAC after a v1 sign-in, v3 bound to the device id after v2. Signing is one-way (`lib/LockboxSecurity/src/SigningPolicy.h`): once a box has stored a command key (`identity/cmdSigned`) or has a burned one, an `/auth` 200 without `commandKey` keeps the stored key and the box refuses plain JSON even when it holds none. A new key replaces the old one; only an NVS erase resets the ratchet. A box that never held a key and has no burned one takes plain JSON as it always did.
- The command replay ring and last timestamp persist in NVS (`identity/cmdReplay`) after every accepted command and survive sign-ins, so a reboot never makes a recorded command acceptable again. The gate counts as loaded only after the ring was read from NVS: until then (a busy preferences mutex) no command is opened, no report is sent, and a sign-in does not install its key. Once the clock is server-verified (`isTimeSynced`), a command more than 10 minutes old is stale.
- Keep the shared command-envelope vector in `test/test_command_envelope` identical to the server suite. It runs on the host against a test implementation; `test_integration` runs it on hardware against mbedTLS.
- A device holding a command key seals every state report and activity log (`CommandGate::seal`, labels `lkbx-rpt-mac-v2` / `lkbx-rpt-enc-v2`): r2 over `r2.{ts}.{MAC}.{iv}.{ct}` after a v1 sign-in, r3 over `r3.{ts}.{deviceId}.{k}.{n}.{iv}.{ct}` (kind `state`/`log`, 16-byte random nonce) after v2. The dashboard refuses plain reports from a hardware-bound device, and a box with a burned key never sends one: a report it cannot seal is dropped. Keep the report and v3 command vectors in `test/test_command_envelope` identical to rad-app's suites. The `/firmware` provenance and the last will stay plain.
- Task stacks come from internal RAM, which TLS and AES DMA also need; on staging hardware the boot peak leaves only a few KB. The MQTT client task runs with 10 KB (`mqtt_cfg.task_stack`: 6 KB overflowed opening envelopes, 12 KB could not be created at boot) and `syncLockState`/`mqttConnect` with 10x the minimum (the v2 state parse overflowed 6x). Its message buffers are 8 KB so they come from PSRAM: allocations of up to 4096 bytes stay internal (`SPIRAM_MALLOC_ALWAYSINTERNAL`). Before growing any stack or internal buffer, measure `uxTaskGetStackHighWaterMark` and `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` on a staging build on both rig units. The MQTT client start retries, and a boot sync that cannot start carries on as a failed pull.
- Retained messages on the command topic are dropped in both modes.
- A failed or refused authentication (401 backs off to 60 seconds) only keeps the device off the broker. It must never lock, unlock or discard a session.

**HTTP (Server Sync):**
- `GET/POST {RAD_SERVER}/api/lockbox/state/{MAC_ADDRESS}`
- TLS in production (DigiCert root CA), insecure in dev

**WiFi:**
- WiFiManager captive portal: AP name "Lockbox Setup"
- Power saving: `WIFI_PS_MAX_MODEM`

### NVS Preferences

Persistent storage with mutex protection. Namespaces: `lockbox`, `session`, `mqtt`, `identity`. Keys defined in `PerferenceKeys.h`. `identity` also records the dashboard's final answer to the one-time key request (`v2Refused`, `v2RetryAt`) and the last E-LOCK-999 code (`v2Failure`); erasing NVS lets a unit without a key ask once more.

### Error Codes

| Code      | Meaning                                 |
|-----------|-----------------------------------------|
| E-LOCK-1  | Could not lock                          |
| E-LOCK-2  | Could not unlock                        |
| E-LOCK-3  | No backplate detected                   |
| E-LOCK-10 | WiFi connection failed                  |
| E-LOCK-11 | MQTT broker connection failed           |
| E-LOCK-12 | Dashboard health check failed           |
| E-LOCK-13 | Dashboard sign-in rate limited (429)    |
| E-LOCK-14 | Dashboard sign-in failed                |
| E-LOCK-20 | Emergency unlock used (24hr lockout)    |
| E-LOCK-999 | Device key could not be stored in eFuse (first setup only; signs in with v1) |

## Build Configuration

### Environments

| Profiles | Purpose | Hardware |
| --- | --- | --- |
| development-r2/r8 | Isolated local services | Verified R2/R8 |
| staging-r2/r8 | Staging dashboard; PR bench observation | Both R2 and R8 |
| production-r2/r8 | Production dashboard; compile on every PR | Both R2 and R8 |
| test, test_native | Native software checks | Host only |

### Build Scripts

- `software/firmware/pre_build_script.py` applies `software/scripts/patch_sdk_security.py` to every device profile and, for development profiles only, force-includes the generated `software/dev/.dev/local/include/lkbx_dev_config.h` from `software/dev/dev_services.py`. Outside CI, each development build reconfigures for this machine's current private LAN IP (set `LKBX_DEV_LAN_IP` to pin one), and a digest of that header forces a rebuild whenever it changes. Development builds never contact cloud services.
- `dev_services.py up` (also run by development uploads and device tests) serves the generated endpoints from a local RAD App when EMQX (`:18083/status`) and RAD App (`:3000/api/health-check`) both answer on loopback, through `fixture/rad_app_front.py`, which reuses the local certificate, forwards only to those host ports, drops the fixture bearer and answers `/api/firmware/*` with no update. Otherwise it falls back to the Mosquitto fixtures. `--backend`/`LKBX_DEV_BACKEND` (`auto`, `fixture`, `rad-app`) overrides; under CI, `auto` always uses `fixture`. The firmware header is identical for both backends.
- Local fixtures support `configure --api-port <port> --mqtt-port <port>` to coexist with other bench services. Reconfiguration preserves the selected primary device and ports; Windows configuration directories use explicit user/SYSTEM ACLs because `chmod` does not restrict Windows access.
- The pinned ESP-IDF 4.4 mbedTLS hostname verifier checks only dNSName SANs. Local certificates include exact literal-IP dNSNames alongside proper IP SANs; certificate-profile migration preserves the existing CA, leaf key and validity interval so firmware trust and cached clocks remain usable.
- Older OpenSSL 3.x micro-CA mode requires self-signed certificate input. Migrate local leaf names through private transient signing, and atomically install only after the existing CA, IP/DNS names, public key and exact validity dates pass verification; never install or trust the transient certificate.
- ESP-IDF builds on Windows require project and PlatformIO core paths without spaces, such as an isolated checkout under `C:\rad-hil\projects` and `PLATFORMIO_CORE_DIR=C:\rad-hil\platformio`.
- `firmware_build_metadata.py` (with `serial_identity_build.py`) stamps firmware provenance and serial identity.

### Flash Partitions

`partition.csv`: NVS (20KB), OTA data (8KB), App0/App1 (6.25MB each), SPIFFS (3.375MB), Coredump (64KB)

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

- Native firmware (from `software/firmware/`): `pio test -e test` and `pio test -e test_native` (Unity + ArduinoFake; suites under `test/`).
- Tooling: `python -m unittest discover -s software/scripts -p 'test_*.py'`, `python -m unittest discover -s software/dev -p 'test_*.py'`, `python -m unittest discover -s software/scripts/hil -p 'test_*.py'`, and `python -m unittest discover -s .github/scripts -p 'test_*.py'`.
- Local services: `python software/dev/fixture/test_http_fixture.py` and `python software/dev/fixture/test_rad_app_front.py`, then with `software/dev/dev_services.py up --backend fixture --lan-ip 127.0.0.1 --device-id AA:BB:CC:DD:EE:FF --device-id AA:BB:CC:DD:EE:FE`, `python software/dev/fixture/test_compose_integration.py`.
- Sanitizer suites: `python software/scripts/test_http_worker_security.py` and `python software/scripts/test_firmware_runtime.py`.
- Secret/boundary scan: `python software/scripts/check_software.py` (needs gitleaks).

## Important Rules

- Never commit credentials or certificates
- Motor pins are extern and may be swapped by calibration — don't hardcode assumptions
- Display and preferences access must use their respective mutexes
- State machine is the single source of truth for device behavior — all logic flows through `LKBX.h`
- Report a break from the common `locked.break` entry after its start timestamp is persisted; dashboard requests bypass the local break-preflight page. Native dispatch tests use the exact shared transition fragment.
- BLE `connectivity.server` describes the last completed configured-RAD HTTPS operation (2xx, plus a valid Date for clock probes). It has no inferred expiry; Wi-Fi disconnect invalidates it and stale in-flight results cannot restore it. It is telemetry and never authorizes device behavior.
- Pairing QR screens use a RAM-only authenticated code lease and `pairingCodeExpiresAt` UTC expiry, clear the QR before expiry, and refresh in a bounded worker. Never substitute a permanent MAC or production URL for an unavailable code. Development QR links use the private `--dashboard-origin`; release links use their explicit RAD origin.
- BLE provenance defaults to bounded v2 metadata; retrieve the complete signed proof through reference-bound 64-byte base64url pages. Preserve signature/runtime-image verification, NVS and the 509-byte transport cap. A metadata summary alone is not verified proof.
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

- PRs into `staging` or `main` that change the firmware directory, or the validation tooling (`software/scripts/hil/`, `pr-firmware.yml`, `hardware-run.yml`, `.github/scripts/hil_evidence.py`), build every hardware variant and run the exact staging bundle for the PR merge SHA on the shared Test Rig. Other PRs skip both; `Build validation` and `Hardware validation` then pass without work.
- `PR target policy`, `Build validation` and `Hardware validation` are required on `staging` and `main`.
- The rig records the product's camera for the whole run; `Hardware evidence` comments on the PR with each device's video and result, pass or fail. Video never decides a result.
- Merging a firmware change into `main` allocates the next version and publishes a signed candidate through `publish_firmware.yml`; staging merges do not.
- Firmware bundles and hardware evidence are Actions artifacts, never tracked files.
- The reviewed harness and fixture inventory live outside the runner work directory.
- Never infer product identity or flash capacity from a COM port or advertising name.
- Follow software/scripts/hil/README.md for enrollment, shared-device locks, and rollout.
- The shared rig (`DESKTOP-SGS6GOQ`, `C:\rad-hil`, runner `lkbx-rig-01`) serves every firmware product. `software/scripts/hil/` (including `catalog.py` and `reviewed-workflows.json`) is identical across LKBX, DTT, OSSM and RADR; change them in all four together, reinstall with `install.ps1`, and update the organization runner group `rad-firmware-validation` allowlist to the exact reviewed `hardware-run.yml@<sha>` refs.
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
