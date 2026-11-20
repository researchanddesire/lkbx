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
- Devices download artifacts only from the dashboard's own Storage hosts, `acjajruwevyyatztbkdf.supabase.co` (production) and `meuaxbjzqrszxdvmacug.supabase.co` (staging), on every build track (`TRUSTED_ARTIFACT_HOSTS` in `lib/FirmwareUpdateProtocol`): the control plane can move a box to the other track. Moving firmware Storage to another host (a custom domain, a new project) needs a firmware release that trusts it first.
- `publish_firmware.yml` runs on firmware-input pushes to `main`: it allocates the next version, commits `AUTO: Version bump main <version>` as `rad-version-control[bot]`, builds R2 and R8, and publishes immutable *validating* candidates with signed provenance. Do not hand-edit `software/firmware/src/constants/Version.h` to release.
- `finalize_release.yml` (manual, production) tags a main release only after the control plane reports both variants ready with build, unit, integration, artifact and hardware validations passed. Rollout stays a control-plane action.
- Environments: `firmware-staging` (branch `staging`) and `firmware-production` (branch `main`), each holding `FIRMWARE_PUBLISH_TOKEN` and `FIRMWARE_PROVENANCE_SIGNING_KEY_PEM`. The `rad-version-control` app credentials are organization-level. Never print, log or commit secret values.
- `.github/scripts/firmware_provenance_test_vector.json` is signed by a throwaway key (kid `rd-fw-test-vector-2026-11-04`, private half destroyed), never a release key. Tests trust its `publicKeyPem` explicitly; the release trust maps in the scripts and `FirmwareProvenance.h` must never list it. To change the claims, sign a new vector with a new throwaway key. Never sign test data with a release key.

## Supabase

- The only Supabase project agents and workflows address is `acjajruwevyyatztbkdf`, through the organization secret `SUPABASE_ACCESS_TOKEN`. Name it explicitly in every remote operation; never infer a project from a URL, environment variable, cached CLI link, neighboring repository or prior conversation.
- Firmware releases reach it only through the dashboard firmware API. PR hardware evidence videos go to its private `hil-evidence` bucket.

## Documentation (rad-app hub spoke)

- `assembly-docs/**` on `main` is authoritative for the LKBX assembly site. The rad-app hub imports this spoke's current `main` head (product `lockbox`, path `assembly-docs`); it keeps no spoke SHAs or baselines. `trigger-assembly-docs.yml` notifies the hub on `main` pushes only.
- Documentation changes land on `main` only and reach `staging` through the main → staging back-merge; `main` wins docs conflicts. Never sync or reconcile docs on `staging`.
- Every `assembly-docs` change on `main`, including a deletion, reaches the live site within minutes without review. Ask AJ before any change that could remove written documentation.
- Keep `assembly-docs/_bom/source.json` at `{"repo": "Open-Source-Chastity-Lockbox_LKBX", "commit": "main"}`. Whenever `_bom/bom.csv` changes, regenerate `_bom/bom.json` (same repo/commit, columns and rows parsed from the CSV) in the same commit, or the hub import fails.
- Never rewrite `main` history, and keep `trigger-assembly-docs.yml` on `main`.
- `developer-docs/**` is not published automatically. The old `trigger-dev-docs.yml` dispatch was removed on 2026-11-04: its token was invalid and `researchanddesire/dev-docs` no longer listens for it.
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
- **Comms:** MQTT (esp_mqtt_client), WiFi (WiFiManager), BLE (NimBLE — on from boot and kept beside MQTT on every build profile; released only for a firmware update)
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

Tasks created in `setup()` (`main.cpp`) that run for as long as the box is up:

| Task    | Core | Purpose                                      |
|---------|------|----------------------------------------------|
| Delay   | 1    | Timed steps (`Done`), the Chaster question, the state read, the deadline of a locked page whose countdown task is missing |
| Button  | 0    | Process button inputs (25ms tick, max prio)  |
| Idle    | 0    | Power management (idle → sleep transitions)  |
| Activity log | 0 | Sensor and state logs, countdown restore, report retries (the emergency report included), Wi-Fi setup portal, MQTT restart, Wi-Fi station keeper, network recovery |

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
- A device holding a command key seals every state report and activity log (`CommandGate::seal`, labels `lkbx-rpt-mac-v2` / `lkbx-rpt-enc-v2`): r2 over `r2.{ts}.{MAC}.{iv}.{ct}` after a v1 sign-in, r3 over `r3.{ts}.{deviceId}.{k}.{n}.{iv}.{ct}` (kind `state`/`log`, 16-byte random nonce) after v2. The dashboard refuses plain reports from a hardware-bound device, and a box that signs (a burned key, or a v1 box that has held a command key) never sends one: a report it cannot seal is dropped. Keep the report and v3 command vectors in `test/test_command_envelope` identical to rad-app's suites. The `/firmware` provenance stays plain.
- The last will is built before every connection attempt (`MQTT_EVENT_BEFORE_CONNECT`), never once per boot: the client reconnects on its own, and a will reused across connections reads as a repeat of the first disconnect. It is published on the MQTT client id, QoS 2, retained, as before. A box that holds a command key (keyed v1 and v2) registers `{"v":4,"k":"will","ts","n","iv","ct","sig"}`, signed `r4.{ts}.{clientId}.will.{n}.{iv}.{ct}` with the report MAC key, where `ct` is the report encryption of `{"id":"{n}","state":"disconnected","createdAt":0,"firmwareProvenanceId"}` and `n`, `iv` and `ts` are new for every attempt (`CommandGate::sealLastWill`). A box that has never signed keeps the plain will, with a new `id` per attempt. A box that signs never registers a plain will: without a key or with a clock before 2024 it connects without one (`utils/lastWillPolicy.h`). `buildLastWill` in `utils/lastWill.h` is the only place that chooses the will; `applyLastWill` hands it to the client through `esp_mqtt_set_config`, which also replaces the TLS settings and task stack, so it starts from the settings `rememberMqttSettings` kept. Keep the will vector in `test/test_command_envelope` identical to rad-app's `lastWill.spec.ts`.
- Task stacks come from internal RAM, which TLS and AES DMA also need; on staging hardware the boot peak leaves only a few KB. The MQTT client task runs with 10 KB (`mqtt_cfg.task_stack`: 6 KB overflowed opening envelopes, 12 KB could not be created at boot) and `syncLockState`/`mqttConnect` with 10x the minimum (the v2 state parse overflowed 6x). Its message buffers are 8 KB so they come from PSRAM: allocations of up to 4096 bytes stay internal (`SPIRAM_MALLOC_ALWAYSINTERNAL`). Before growing any stack or internal buffer, measure `uxTaskGetStackHighWaterMark` and `heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)` on a staging build on both rig units. A boot sync that cannot start carries on as a failed pull. An MQTT client whose task does not fit at boot is kept, not destroyed: the sign-in task tries briefly (3 times for a box that has been set up, 10 during first setup), then hands it to the activity log task (`retryMqttStartLater`), which starts it every 2 s until it fits, and ends itself so its own 8.3 KB stack is free for the client's. Never go back to destroying the client on a failed start: the box then stays off the broker until it restarts.
- A task's stack is one contiguous block of internal RAM, and on R8 the largest free block is at times under 8 KB (2026-11-15 rig run: 7.7 KB; 183 bytes free in all with the Wi-Fi portal and MQTT both up). Never assume `xTaskCreate*` succeeded on a UI or lock path: create through `startTaskChecked` / `startTaskWithRetry` (`tasks/TaskStart.h`), which log the largest free block, and give the failure a way out (an error page, a fallback event, a later retry). A boot step that cannot start its task reports itself done through the delay task; a locked timer's countdown task is restarted by `restoreUnlockTimer` while it is missing, and for as long as it still cannot be created the delay task watches the deadline in its place (`LKBX::serviceCountdownWatch`, `utils/countdownWatch.h`): a local timer is released, Ready to unlock is shown and the emergency lockout ends on time, and only the drawing of the time left waits for the task (2026-11-16 rig run on R8: the task could not be created 22 times in a row). The task is not given a stack of its own for good: that would take about 5 KB of internal RAM at all times, boot included. Prefer running short work on a task that already exists over creating one: the Chaster question and the state read (`confirmServerClear`) run on the delay task for this reason, and the activity log task opens the Wi-Fi setup portal and repeats unsent reports. The delay task and the activity log task are created in `setup()` and the box restarts if either cannot be.
- The state read (`LKBX::confirmServerClear`, `utils/stateRead.h`) only notes that a read is wanted (after a start of the MQTT client, after every reconnect, and where an MQTT message cannot be applied as it is); `LKBX::serviceStateRead` runs it on the delay task, one non-blocking step per 100 ms pass (`startSessionPull` / `takeHttpResult` / `applySessionResponse`). It is tried until one read has completed: every 5 s, after 6 failures in a row every 30 s, and it never gives up. A read wanted while one is in flight gets its own. Local play skips it, and an answer that arrives after a local session started is dropped. Do not move it back to a task of its own: after a reconnect the 8.3 KB `confirmClear` task could not be created 5 times in 6 (2026-11-16 rig run).
- A state report that cannot be sealed or queued (no internal RAM for AES right after a reconnect) is not dropped: `sendMessage` keeps the newest one in plain form (`utils/unsentReport.h`, storage from PSRAM, nothing in it allocates or throws) and the activity log task seals and sends it again every 5 s while connected, 12 times at most. Any later state report replaces it. LOCKED keeps its own tracker; DISCONNECTED (sent on the way to sleep) is neither kept nor clears the kept report. EMERGENCY is the one report kept in flash (`utils/emergencyReport.h`, see "Emergency unlock" below).
- One secure connection is set up at a time (`utils/tlsGate.h`, `services/tlsTurn.h`). A TLS handshake is the most internal RAM a connection ever needs, and R8 does not have enough for two at once (2026-11-16 rig run: `esp-aes: Failed to allocate memory` in 5 of 7 reconnects, the MQTT connection dropped, and twice the box stayed without network until it was restarted). The MQTT client takes a turn in `MQTT_EVENT_BEFORE_CONNECT` and keeps it until 1.5 s after `MQTT_EVENT_CONNECTED` (its subscription, provenance record and waiting reports go out then) or until `MQTT_EVENT_DISCONNECTED`; the HTTP worker takes one around `esp_http_client_open`; on staging builds the rig probe (`services/radHil.cpp`) takes one for its request. Every wait is bounded and the caller then connects anyway: MQTT waits 4 s at most (it waits on the MQTT task, which holds the client's lock, so a report queued meanwhile waits too), an HTTPS request 4 s and never more than a third of the time it has left, and a turn nobody gave back ends after 15 s. Any new code that opens a TLS connection of its own must take a turn. Do not hold a turn for longer than the handshake.
- The state is read after every MQTT connection that follows a drop, not only after a start (`MqttReconnect` in `utils/mqttPause.h`, reported by `serviceMqttPause`): the client connects with a clean session, so the broker keeps nothing for a box that was away and a command published during a short drop is never delivered. The first connection since boot is left to the boot sequence.
- A task function ends with `vTaskDelete(nullptr)`, which never returns, so nothing declared in the task function is ever destroyed: a `String`, a `GFXcanvas`, a `JsonDocument` or a sign-in answer declared there stays allocated for good each time the task ends. The countdown task's 1,920-byte canvas leaked this way every time the box left its locked pages, about 6 KB of internal RAM per Wi-Fi page visit and unlock, until TLS and task creation failed on R8. A task that owns memory does its work in a `run*` function and only calls that and `vTaskDelete` (`LKBX::runUnlockTimer`, `runMqttConnection`, `runWifiConnection`, `runHello`, `runBreakTimer`). Allocations of up to 4096 bytes come from internal RAM.
- Secure connections take turns (`utils/tlsGate.h`, `services/tlsTurn.h`): the MQTT client while it connects and settles, an HTTPS request from before it connects until its connection is closed. The HTTP worker runs one request at a time, so at most one HTTPS session is open beside the MQTT session. The rig probe (`services/radHil.cpp`, staging builds) connects only with a turn of its own and with at least 20 KB of internal RAM free and a 4 KB block; otherwise it skips and looks again five seconds later. Hardware AES bounces every TLS record through two internal blocks of up to 1.6 KB, because the record buffers are in PSRAM; `esp-aes: Failed to allocate memory` means internal RAM was gone at that moment.
- `services/memoryWatch` notes every failed allocation that had to be internal (task stacks, AES DMA blocks, driver buffers) and logs the internal RAM picture once a minute (`MemoryDiagnostics: Internal RAM: ...`). After a failed allocation, network recovery asks the dashboard 20 seconds after the broker is lost instead of 60 (`utils/networkRecovery.h`); the steps themselves do not change.
- The MQTT outbox keeps a copy of every queued message in internal RAM and is only sent from, and expired, while the client is connected (it is emptied when the client's task stops, not when its connection is closed). Off the broker, activity logs are queued only until the outbox holds 4 KB (`utils/logBacklog.h`); state reports are always queued.
- A menu page whose task could not be created gets it once memory is back (`LKBX::restoreMenu`, `utils/missingTask.h`), as a locked page gets its countdown (`restoreUnlockTimer`). When the motor's current watch cannot get a task it runs on the calling task, so the paddle is still pressed home before the motor is switched off.
- Retained messages on the command topic are dropped in both modes.
- The broker returns every state report on the topic it was published to, which is the command topic. `openCloudCommand` drops a frame that is one of the last reports the box sealed (`utils/ownReports.h`) without a log line; every other frame is still opened and its refusal logged. Do not change the topics to avoid this: they are the dashboard's contract.
- A failed or refused authentication (401 backs off to 60 seconds) only keeps the device off the broker. It must never lock, unlock or discard a session.

**HTTP (Server Sync):**
- `GET/POST {RAD_SERVER}/api/lockbox/state/{MAC_ADDRESS}`
- TLS in production (DigiCert root CA), insecure in dev

**Chaster sessions ask before they open (`utils/chasterAsk.h`, `state/ChasterAskTransitions.inc`):**
- The dashboard marks a session Chaster controls with `chasterControlled` in the HTTP state and the MQTT sync; the older `requiresPermission` means the same, and either one being true makes the box ask. The box stores it with the session (`session/chaster`); a payload with neither field keeps the stored value, and a new session starts without it.
- Such a session never opens on the box's own decision. Before a break from the wearer's menu, and before unlocking at the end of its timer, the box asks: `POST /api/lockbox/v2/ask` `{deviceToken, sessionId, action}` after a v2 sign-in, `POST /api/lockbox/ask` `{macAddress, deviceSecret, sessionId, action}` otherwise, with `action` `break` or `unlock`. The answer is `200 {ok, sessionId, action, allowed, code, reason}` and must repeat the session and action it answers.
- Only `allowed: true` opens the box. A refusal shows Chaster's reason; `code: "unavailable"`, any other status, a timeout, no Wi-Fi or a malformed body is no answer, and the box stays locked on a page that says so. Offline, a Chaster session never unlocks or opens for a break. `code: "no_session"` makes the box read its state again.
- Emergency unlock, a clear from the dashboard and local (non-Chaster) sessions never ask. Every answer page leads back to the lock menu, where emergency unlock is. Do not add an automatic unlock after a time offline.
- The dashboard holds a session Chaster would refuse to unlock through the freeze fields, with a reason that starts with `Waiting for Chaster`. The held page then reads "WAITING for Chaster" instead of "FROZEN by Chaster", and its reason line shows only the detail.
- A break the dashboard starts (`Break`) opens the box from every locked page. On a Chaster session a break does not end because the box's own deadline passed.
- The question runs on the delay task (`LKBX::serviceChasterAsk`), one non-blocking step per 100 ms pass: `postDetached` hands the request to the HTTP worker, `takeHttpResult` is polled, and the answer's transition runs on the delay task's stack. No task is created per question. A question the HTTP worker cannot take is handed over once more after 500 ms, then reported as no answer.

**WiFi:**
- WiFiManager captive portal: AP name "Lockbox Setup". The setup network is open, so the portal serves only the root page, the network form, its save and exit; WiFiManager's other routes (`/erase`, `/restart`, `/info`, `/param`, `/paramsave`, `/close`, `/status`, `/update`, `/u`) answer 404 (`lib/LockboxSecurity/src/PortalRoutes.h`).
- Power saving: `WIFI_PS_MAX_MODEM`
- The setup portal and a connected MQTT client do not fit in R8's internal RAM together, and the portal's own task (8.3 KB stack) does not always fit beside the client either. So a Wi-Fi setup page only asks for its portal (`requestWifiPortal`, from the page's entry action), and the activity log task opens it (`serviceWifiPortal`, `services/wm.cpp`): it takes the client off the broker (`pauseMqttForPortal`), then creates the portal task, retrying while the client's memory comes back. If the task still cannot be created the client connects again and the page shows "Wi-Fi Setup Not Open" instead of a QR code for a network that does not exist. One portal at a time: a page left and entered again gets a new portal after the old one has closed. The activity log task has the client connect again two seconds after the last portal closed (or timed out with its page still up) (`serviceMqttPause`, `utils/mqttPause.h`). Each connection is a new one with its own last will. Once connected again the box asks for one state read (`confirmServerClear`), for whatever was published while it could not listen. Never call `pauseMqttForPortal` or `releaseMqttTaskForPortal` from the MQTT task or a state machine action, and never create the portal task before the client is off the broker.
- Pausing the client closes its connection and keeps its task (`closeClientConnection` in `services/mqtt.cpp`): auto-reconnect is turned off (`esp_mqtt_set_config`, which replaces every setting, so `applyLastWill` carries the same flag), then `esp_mqtt_client_disconnect`, a clean MQTT disconnect, so the broker publishes no last will. In the pinned esp-mqtt (ESP-IDF 4.4.7) that closes the transport, which frees the TLS session, and leaves the task waiting in `MQTT_STATE_WAIT_RECONNECT`; `esp_mqtt_client_stop` instead ends the task (`vTaskDelete`) and `esp_mqtt_client_start` creates a new one with a new 10 KB stack. Stopping it used to give the portal that stack, but the block had to be found again in one piece afterwards, and on R8 it was not always there: `Error create mqtt task` every 2 s for more than ten minutes, with at most 5.6 KB in one piece, until the box was reflashed (2026-11-16 rig run). A task that never ends never has to be created again; resuming is `esp_mqtt_client_reconnect`. The request to disconnect is dropped when a connection attempt begins, so `serviceMqttPause` repeats it for a kept client that turns out to be connected. The task is stopped after all only when the portal's own task cannot be created beside it (`releaseMqttTaskForPortal`), when the client does not let go of its connection within 5 s, and for network recovery step 1 and a firmware update; a client stopped that way needs a new task afterwards, tried every 2 s. Do not lower the 10 KB stack to make it fit: 6 KB overflowed, and one rig measurement (5.7 KB left free) is not a peak. Do not go back to stopping the task for every portal.
- The Arduino core connects the station again once per disconnect event, and not at all in three cases: that attempt fails without a further event, the access point sends the station away with reason 8 (which the core takes for the box's own wish), or the setup portal was opened while the box was offline (WiFiManager turns the station off and on closing reconnects only from `WL_IDLE_STATUS`, which the core does not report there). So the activity log task begins a station that has stayed down for 30 s again with its saved credentials, every 30 s (`serviceWifiStation`, `utils/stationKeeper.h`), never while a setup portal is open or wanted. The disconnect reason and the memory picture are logged (`Wi-Fi station disconnected (reason N)`, at most every 10 s). The firmware never turns Wi-Fi off on purpose; if that changes, the keeper must be told.
- A box can be cut off while its station stays associated and no disconnect event comes, which the station keeper never sees (2026-11-16 rig run on R8: connections timed out, then every name lookup failed, for 8 and for 40 minutes, with internal RAM in pieces of at most 3.2 KB). The activity log task brings such a box back in steps (`serviceNetworkRecovery` in `services/wm.cpp`, `utils/networkRecovery.h`), each logged as `Network recovery: ...` with the memory picture. Off the broker for 60 s with the station up, it asks the dashboard (`GET /api/health-check`, no identity, 15 s). Any HTTP response means only the broker is out of reach: nothing is done and the question is asked again after 5 minutes. No response is step 1: the MQTT client is stopped (its task stack and TLS session come back) and started again through `serviceMqttPause`, and the station is connected again (`WiFi.reconnect()`). Still off the broker 120 s later, counted from when the station is back up, and still no response is step 2: the box restarts (`esp_restart`) once it is at rest. Being back on the broker ends it at any point.
- A dashboard that answers does not prove the fault is the broker's: the box's own MQTT client can be the part that is missing. `mqttClientCannotStart()` (`services/mqtt.cpp`) is true while the client is meant to run and the last attempt to create its task failed (not while a portal holds it, and not while it only waits for its next attempt). When that has lasted 120 s with the station up (`kClientDownMs`), network recovery wants the same restart at rest, whatever the dashboard said (`RecoveryStep::CLIENT_DOWN`, logged as `Network recovery: the MQTT client could not be started for two minutes; restarting once the box is at rest`). On the 2026-11-16 rig run the dashboard answered HTTP 200 twice while the client could not be created, the box logged "only the broker is out of reach; nothing to do" and stayed off the broker for good.
- The recovery restart is the only restart the firmware makes on its own judgement. It is safe for the reason a deep-sleep wake is: lock state, session and deadline are in NVS and the boot sequence restores them; the wall clock survives a software restart. It only happens when all of this holds for 5 s without a break (`atRestForRestart`): no button, dial, cable or message for 30 s (`idleState` is not `NOT_IDLE`); the motor is off (no `currentWatch` task); no setup portal is open, closing or wanted; the page is `home.idle`, `locked`, `locked.timer`, `locked.ready` or `locked.frozen`; and the box holds no local session (`isLocalPlay`). The emergency lockout page is left out as well. Both exclusions date from when a boot ended a local session and locked a session again after an emergency unlock; a boot now keeps both (see "Emergency unlock and local sessions across a restart"), but the exclusions stay until that has been confirmed on the rig. It never happens while the station is down (an access point that is off must not restart the box), never before the box has been on the broker once since it started (so a restart that did not help is not followed by another), and never on a "no response" older than a minute. Reports that were still waiting in RAM are lost by it, as by any restart; the decisions the dashboard needs (locked, declined, completed) are repeated from NVS after the next connection. Do not add pages to the list without checking that a boot leads back to them with the lock unchanged.

### Emergency unlock and local sessions across a restart

Every boot of an online, paired box asks the dashboard for its session (`syncLockStateOnBootTask`), and so does the state read after a reconnect. What the dashboard says must never undo what the wearer did on the box. The rules are in `utils/emergencyRecord.h` (decisions, unit-tested in `test_emergency_record`) and `utils/emergencyStore.h` (the record in flash).

- **A local session is never ended by the dashboard's answer.** A box that holds a local lock (`isLocalPlay` and `isLocked`) does not ask at boot (`bootKeepsLocalSession`): it goes on to its saved lock, as it does offline. The dashboard has no session for a local lock and answers `clear: true`, which cleared the stored lock and opened the box before its timer had run out (2026-11-16 rig run on R2, every restart with Wi-Fi up). The state read skips a local session too, and an MQTT sync is only applied when it names another session, which is the existing rule for a new offer. A local session ends when its timer runs out (`TimerExpired`, `acceptLocalTimerUnlock`) or through emergency unlock.
- **A session ended by emergency unlock is never locked again.** The emergency unlock records the session it ended (`lockbox/eSession`; empty for a local session). `saveState`, the one place a dashboard session is stored, refuses that session (`refusesEmergencyEndedSessionLocked`): nothing is stored, the motor does not move, and the report is owed again. Before, a box whose EMERGENCY report was lost restarted online, was told the session was still locked and drove the paddle back to locked under the "Emergency Unlocked" page (2026-11-16 rig run on R2). Never add a path that stores a dashboard session without going through `saveState`.
- **The EMERGENCY report is owed until the broker has acknowledged it.** It is written to flash in plain form before it is sent (`lockbox/eReport`, `lockbox/eOwed`; `LKBX::reportEmergencyUnlock`) and the activity log task seals and sends the same bytes (`LKBX::serviceEmergencyReport`, `utils/emergencyReport.h`): at once, again every 10 s without an acknowledgement, 12 times per connection. The dashboard knows a report by its `id` and answers a second copy with "Event already tracked", so repeats are harmless. A report for a dashboard session is sent at the emergency unlock itself and after that only while the dashboard is seen to still hold the session: after every new connection and after a restart it waits for the state the box reads (`Owed::onConnected`, `Owed::restore`), and the refusal above is what sends it (`Owed::owe`). If that state has not come after 30 s on a live connection (the boot's read failed), the state read is asked for once. Do not send it on connecting alone: whether it has gone out is how the box tells the two clears below apart. A report for a local session goes out on any connection.
- **What a dashboard clear does to the lockout.** The dashboard ends the session in answer to every emergency report, and a database trigger then publishes a `clear` for it; a state read answers `clear: true` from then on. That answer must not end the lockout the report announces. So: while the report was still owed, "the session is no longer active at the dashboard" means the dashboard ended it by itself and never heard of the emergency, and the lockout is lifted and the report dropped (`EmergencySettle::LIFT_LOCKOUT`); once the broker had acknowledged the report, the lockout stays (`KEEP_LOCKOUT`). Either way the session's name is forgotten, so a session started again under the same id is an ordinary offer. A lockout with no dashboard session on record (a local session, or one already settled) is not touched by a clear. It ends after its 24 hours or when a new lock is accepted on the box, as before. The same rules apply at boot, in the state read and to an MQTT clear (`emergencyLockoutOutlastsClear`).
- A lockout that was already running when a box was updated to this firmware has no session on record: it runs out by time or a new lock, and a dashboard clear no longer lifts it.

### NVS Preferences

Persistent storage with mutex protection. Namespaces: `lockbox`, `session`, `mqtt`, `identity`. Keys defined in `PerferenceKeys.h`. Reading a string key that does not exist logs an error in the Arduino core; look for it with `isKey` first (`emergencyStringLocked`). `identity` also records the dashboard's final answer to the one-time key request (`v2Refused`, `v2RetryAt`) and the last E-LOCK-999 code (`v2Failure`); erasing NVS lets a unit without a key ask once more.

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
| staging-r2/r8 | Staging dashboard; PR bench observation (adds the `radHil` observer: a 6 KB task and its own HTTPS request every 30 s) | Both R2 and R8 |
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
- While a session hides its timer, nothing on the screen may depend on the time left: the box does not wake its screen for the last seconds, sleeps until the deadline itself instead of waking two minutes early, treats the Sleep item the same at any time, does not wake its screen for a sync that leaves the page as it is (`syncWakesScreen`: the dashboard moves a hidden timer's end shortly before it runs out), and does not show an unlock hold before its own deadline has passed (`holdShownOnScreen`). The hold still holds. Keep `test_chaster_ask` and `test_timer_sleep` covering the minutes before the end.
- Emergency unlock triggers a 24-hour lockout period. A session it ended is never locked again by what the dashboard says, and its report is repeated until the broker has it (see "Emergency unlock and local sessions across a restart")
- A local timer locks only once its deadline is stored: the `saveLocalTimer` guard (`state/LocalTimerTransitions.inc`) commits it through `saveLocalLockTimerDuration`, which reads the value back, and refuses the press otherwise. The timer page's duration is zero until the dial has drawn it. Never move the lock action in front of that guard.
- The lock menu keeps the countdown task of the page it was opened from, and starts it again when it is entered from a page that ends it (the break pages, the emergency page, a Chaster answer), so it still moves to Ready to unlock, or releases a local timer, at the deadline (`lockMenuCountsDown`, `utils/lockMenuCountdown.h`). It does not start it for a managed session that is already ready to unlock: the countdown would send the wearer straight back to Ready to unlock and the menu, with its emergency unlock, could not be reached from the page that says Chaster refused.
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
- PR hardware runs take precedence on the Test Rig: they take a fixture from a bench session and have the OSSM guard clear its own latch before flashing. Hold fixtures only through `rig_lock`, and expect a bench session to be stopped when a PR run arrives.
- The shared Windows Test Rig (`C:\rad-hil`, runner `lkbx-rig-01`; keep its host name and network details out of this repository) serves every firmware product. `software/scripts/hil/` (including `catalog.py` and `reviewed-workflows.json`) is identical across LKBX, DTT, OSSM and RADR; change them in all four together, reinstall with `install.ps1`, and update the organization runner group `rad-firmware-validation` allowlist to the exact reviewed `hardware-run.yml@<sha>` refs.
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
