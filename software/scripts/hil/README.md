# PR firmware and shared bench validation

A pull request into `staging` or `main` that changes the firmware directory, or
this validation tooling, builds the GitHub PR merge commit and runs it on the
shared Test Rig. `RELEASE_BUILD_SHA` is `github.sha`, never the PR head SHA.
Production profiles compile; only staging artifacts reach fixtures. Other pull
requests skip the build and the rig, and `Build validation` and `Hardware
validation` pass without work. Each repository's `FIRMWARE_PATHS` in
`pr-firmware.yml` and `pr-target-policy.yml` defines its firmware directory.

| Repositories | Staging and production suffixes | Physical coverage |
| --- | --- | --- |
| lkbx | r2, r8 | Both 16 MB ESP32-S3 boards, verified R2/R8 PSRAM |
| dtt | v1, v2 | 4 MB and 16 MB profiles; actual chip capacities recorded separately |
| OSSM | 4mb, 16mb | One ESP32, selected by measured capacity |
| radr | existing staging/production aliases | Enrolled 16 MB R8 remote, application and LittleFS |

OSSM retains its original development/staging/production aliases. Its restored
Software tree comes from this repository's main firmware; staging hardware and
assembly documentation remain separate. The Trainer keeps its SDK security
patches and both deployed partition tables.

OSSM's 4 MB profiles use the standard 4 MB min_spiffs layout and size optimization;
16 MB profiles and the existing aliases retain main's custom `min_spiffs.csv`.
Measured capacity alone does not authorize a partition migration. A 16 MB fixture
still carrying the older 4 MB table fails preflight until a separately reviewed,
storage-preserving migration has been completed. Never relabel it as a 4 MB board.
The one exception is the dual-lane OSSM below: release V1 jobs leave the reviewed
4 MB OSSM table on it, and the next 16 MB write puts its own table back. Both
tables keep NVS, otadata and app0 at the same offsets, the write verifies NVS
byte for byte, and the OSSM stores nothing in its filesystem or coredump partitions.

## Interfaces and trust boundary

`catalog.py` is the reviewed policy for products, physical variants, partition
layouts and writable regions. `bundle.py` emits schema 2 `manifest.json` plus
per-variant images. It records repository, merge SHA, run ID/attempt, product,
environment, chip, flash bytes, PSRAM, offsets, byte lengths, SHA-256 checksums,
and the application digest reported at boot. All variants must be present.
Bootloader, partition table, boot-selection image, application, ELF, and RADR's
LittleFS are included. ELF is retained for debugging and never executed/flashed.

Bundles are **data only**. The Windows worker never checks out PR source or runs
downloaded Python, PowerShell, or other artifact files. It calls the installed
`C:\rad-hil\harness\launch.py` with isolated Python. The host's catalogue and
validator, not a manifest supplied by a PR, decide writable offsets and limits.
The manifest must match the caller's repository, merge SHA, run ID and attempt.
Artifact names include the attempt. Use **Re-run all jobs** to create a fresh
bundle; rerunning only a failed hardware job must not reuse an older attempt's images.

The fixture is schema 1 JSON, stored only at `C:\rad-hil\fixture.json`. Its
`products` object has `lockbox` (r2/r8), `dtt` (v1/v2), `ossm` (exactly one of
4mb/16mb), and `radr` (r8). Each device contains:

```json
{
  "product": "dtt",
  "enrolled": false,
  "device_id": "REPLACE_WITH_VERIFIED_FACTORY_MAC",
  "chip": "esp32",
  "flash_bytes": 4194304,
  "psram": null,
  "usb_vid": 4292,
  "usb_pid": 60000,
  "usb_location": "REPLACE_WITH_VERIFIED_USB_LOCATION"
}
```

Supply exactly one of `usb_serial` or `usb_location`; never use a transient COM
number as identity. Set `enrolled` only after physically identifying the product,
reading its factory MAC/chip/flash, and verifying PSRAM where relevant. The owner
has authorized a 16 MB Trainer to exercise the 4 MB V1 image/partition profile.
That V1 fixture must retain `flash_bytes: 16777216` and explicitly set
`allow_larger_flash: true`; boot evidence still verifies the actual 16 MB chip.
This is profile coverage, not evidence of testing a physically 4 MB chip.
The single enrolled 16 MB OSSM may set `allow_larger_flash: true` too. It stays
the one `16mb` OSSM fixture (one capacity, one guard) and then serves both
release lanes, `ossm:v1` and `ossm:v2`; PR runs still flash it as `16mb`. The
exception is refused for every other product/variant or capacity. Missing, duplicate, ambiguous,
or incompatible fixtures fail before writes. Every write rechecks chip, MAC,
flash and PSRAM and reads the installed partition table. A different table
requires a separately reviewed storage-preserving migration; no automatic erase
or repartition fallback is permitted. Approved writes exclude NVS, calibration,
identity, coredump and the inactive application slot. RADR writes its filesystem
alongside its matching application.

`RAD_HEALTH` serial JSON schema 1 is the health interface; product control/BLE
protocols are unchanged. Boot evidence binds the runtime image digest and build
SHA to product/variant, factory MAC, physical flash and staging track. The normal
menu/state task supplies progress, independently of the heartbeat task. Lockbox
readiness also retains the existing requirement for a live staging MQTT connection. Health
requires an idle application, a usable IPv4 address on `IoT_PHB`, and a successful
certificate-verified HTTPS request to staging. Readiness must arrive within 180
seconds, followed by 180 continuous seconds measured by both host time and device
uptime. A heartbeat gap over 20 seconds, stalled progress, network loss, reset,
panic, watchdog, brownout or serial error fails permanently. There is no crash
retry or restarted observation window. Keep Lockboxes unlocked and all devices
idle before starting. No test issues motion/lock commands.

## Provisioning and evidence

Use the existing BLE provisioning service on products that expose it. Obtain the
bench password from the operator's local secret store, never source/build flags,
workflow inputs, artifacts or command-line logs. BLE-disabled Trainers use saved
Wi-Fi settings or their existing setup portal. Verify saved connectivity before
starting the run. Flashing preserves NVS. Do not enable BLE on the public Trainer
to make provisioning easier.

Firmware artifacts expire after 14 days. Evidence expires after 30 days and is
uploaded on success and failure: redacted serial/flash logs, per-device JSON,
aggregate JSON, JUnit and workflow diagnostics. Raw serial output can contain
credentials and stays off uploaded logs. Download/setup failure also produces a
failing JUnit report. Current-run evidence replaces Trainer `test/report.json`
freshness; do not commit generated reports.

### Video evidence and the PR comment

The harness records the camera that sees the product for the whole run: identity
check, flashing, reset and observation. Each device is cropped and rotated from
that stream into `results/video/<variant>.mp4` with an elapsed-time clock, next to
`manifest.json` (camera, views and a timeline of harness events). The PR job
`Hardware evidence` turns every recording into an inline GIF and a web MP4,
stores them in the private `hil-evidence` bucket of Supabase project
`acjajruwevyyatztbkdf` (through `SUPABASE_ACCESS_TOKEN`), and comments on the
PR with 1-year signed links, on pass and fail. Video never changes a result; a
missing camera is reported in the comment.

The camera map is host-only, like the fixture: `C:\rad-hil\cameras.json` names
the ffmpeg binary (`C:\rad-hil\tools\ffmpeg-7.1`), each Brio 100 by serial and
DirectShow path, and per-product views as a normalized `[x, y, width, height]`
crop plus a clockwise rotation. The Brios look down on the bench, so screens need
rotation:

| Camera | Serial | Views |
| --- | --- | --- |
| 1 | 2548APP4WY98 | DTT V1 (right, 180°) and V2 (left, 180°) |
| 2 | 2606APJMNM48 | OSSM motor and rail, before/after stills |
| 3 | 2613APHG9948 | Lockbox R2 (left, 180°) and R8 (right, 180°) |
| 4 | 2613APNG9C58 | RADR (left, 90°) and OSSM screen (right, 270°) |

The USB topology supports one streaming camera at a time, so a product's
devices share one camera. The recorder uses the bench camera service on
127.0.0.1:8765 when it is running and reads DirectShow directly otherwise. An
open Windows Camera app blocks every camera; close it before rig runs.

### OSSM RS-485 interlock

No OSSM image is written unless the independent RS-485 guard
(`C:\rad-hil\rs485-guard`) protects the 57AIM30 motor: its `status.json` must
be under 5 s old, stream bench telemetry at 30 Hz or more, hold a verified stop
path, report no fault, and be in `collision-only` mode with a verified handback,
which lets the firmware home within the guard's hard-stop contact limits. The
harness watches the guard throughout the run and fails it on any guard fault,
latch or stale status. The guard's before/after state is
`results/rs485-guard.json`, and the comment shows motor stills from camera 2.

### Install the release queue worker

Use the reviewed LKBX checkout as the source of the shared worker. Upgrade the
local Test Bench agent with the matching Dashboard revision, then install the
reviewed base harness. Enroll the USB fixtures before starting the worker. The
queue endpoint needs a dedicated `FIRMWARE_VALIDATION_RIG_TOKEN` scoped to
this rig and its deployment. Configure the same value at the console prompt
below. The task stores it using the bench operator's Windows DPAPI account.
The runner service can read only the short-lived admission receipt. Release
claims, tokens and journals are private to the bench operator.

```powershell
# In the bench operator's elevated PowerShell, from the reviewed checkout
.\software\scripts\hil\install-release-queue.ps1 -Track main
```

The task starts in the current session and on operator logon. It runs the
launcher with `-ExecutionPolicy Bypass`, as the hardware workflow does, because
Windows client editions default to the Restricted policy. Before cutover it sends an empty-lane heartbeat and cannot reserve jobs. At idle it
polls every 15 seconds. Inspect `RAD Firmware Validation Queue` in Task
Scheduler and check **Firmware validation** in the admin dashboard for its
heartbeat and enrolled variants. An OSSM candidate that can emit the required physical evidence may boot on the fixture
only when its enrolled factory ID is in the operator-owned JSON array
`C:\rad-hil\release-worker\motion-devices.json`. Independent guard monitoring,
its verified stop path and its verified handback remain required.
The operator can also pause release testing per product with the JSON array
`C:\rad-hil\release-worker\release-families.json`, for example `["radr"]`,
naming the families that may run (`lkbx`, `dtt`, `ossm`, `radr`). Without the file
every family runs. A claimed release job for a family not listed ends blocked,
before any download, lock, flash or other hardware step, with "Release testing for
<Product> is paused on this rig (operator setting)". A file that is not such an
array blocks every release job and names the problem; it never allows everything.
PR jobs and the advertised lanes are unchanged. The worker reads both files when it
starts, so restart `RAD Firmware Validation Queue` after changing either.
The worker checks the SHA-256 of the published application and whether it can emit the required release health records before claiming operator control. On supported binaries it verifies flash readback, resets the enrolled board, and observes the exact running image and continuing application/network progress for 180 seconds. A serial identity reply or a BLE connection alone cannot qualify a release. Missing instrumentation, incomplete coverage and disconnected fixtures are
visible in the queue. No run can certify another board or another binary.

Ordinary production images print one always-on health line, `<PREFIX>_HEALTH {json}`:
`boot` (identity), network events, and a `heartbeat` at most every 5 seconds with
`main_menu` and `menu_age_ms`. `release_health.py` picks the product's production
observer when the image contains its `boot` and `heartbeat` records (the Lockbox:
also its network records), before falling back to the staging-only `RAD_HEALTH`
observer. Readiness needs these events before the 180-second soak, and a repeat
of any of them during the soak fails the run:

| Product | Prefix | Before the soak |
| --- | --- | --- |
| lkbx | `LKBX_HEALTH` | `wifi_connected`, `mqtt_connected` (production broker) |
| ossm | `OSSM_HEALTH` | `wifi_connected`, `mqtt_connected` (production broker), heartbeat `hat` and `homed` for the case |
| dtt | `DTT_HEALTH` | `wifi_connected`, `cloud_ok` (the app's own `/health-check`) |
| radr | `RADR_HEALTH` | `wifi_connected` |

A damaged record (cut by the reset, merged with the ROM banner, or split by
another writer) never counts as evidence and never starts or extends a deadline.
Before this boot's record, damaged records and the previous boot's records are
ignored. After it, more than five damaged records fail the run; fewer are counted
in the soak check.

A board that booted the candidate and kept sending heartbeats on time (uptime
advancing, no reset, fault or identity mismatch) but never became ready within
180 seconds is **blocked**, not failed: the cause is its environment, such as a
rig Lockbox whose device key production does not accept. The detail names what
never arrived, for example "Readiness not reached in 180 s: production MQTT never
connected; main menu never reached (heartbeats continued)"; OSSM details start
with the HAT case. A reset, panic, watchdog or brownout, an identity mismatch, a
heartbeat gap over 20 seconds, uptime that stops, no heartbeat at all, readiness
lost or a reconnect during the soak, and repeated damaged records still fail.

Every OSSM release job runs two cases on the enrolled board, and passes only if
both pass. Each case sends a serial HAT setting, which the OSSM acknowledges with
`RAD_HAT {"mode":...,"applies":"next_boot"}`. Only a reply naming the requested
mode counts: a refused write adds `error` and names the unchanged mode. Refusals
are retried while the carriage homes; if the armed motor guard still refuses, the
worker resets the board and writes the setting before homing starts. It then waits
for the carriage to finish homing, checks the guard again, and resets and observes
boot, homing and the soak:

1. **With HAT**: `hat auto`. The guard polls the servo, so the HAT only listens
   and motion stays on STEP/DIR, as in PR runs. Boot reports `hat_mode` `auto`;
   heartbeats need `hat: true` and `homed: true`, and a HAT that takes the bus fails.
2. **No HAT**: `hat off`. The OSSM never transmits on RS-485 and homes on its own
   current sensor. Boot reports `hat_mode` `off`; heartbeats need `hat: false`
   and `homed: true`.

The rig's Radar is kept away from the OSSM for the whole job. A running Radar
connects to a booting OSSM over BLE and asks for a mode, which starts a second
homing that the guard latches on. Before the OSSM is probed, flashed or reset,
the worker takes `radr.lock` (it never preempts a holder), stops bench monitoring
of the Radar and holds the enrolled Radar in its ROM loader, checking that it
answers with its factory MAC. The Radar stays there through both cases and the
restore, and is then reset into its application and monitored again, on every
path out. If the Radar is not enrolled, its lock is held or it cannot be held,
the OSSM job is blocked before anything moves. The evidence lists "Radar held in
its bootloader so it cannot connect to the OSSM" and whether it was released.

Afterwards, including after a failure, the worker sends `hat auto`, resets and
waits for the homed boot. If the guard is not ready at that point it never
resets the board: it parks the OSSM in its ROM loader and reports the job
blocked. The evidence lists each case's checks ("With HAT: ...", "No HAT: ...")
and whether HAT detection was restored. An image without `OSSM_HEALTH` or the
serial HAT switch is blocked before anything is written. The OSSM is never run as
RS-485 bus master with serial motion (a HAT without the guard); that mode is not
qualified. A V1 job on the 16 MB board expects boot `variant` `4mb`, as a DTT
V1 job on its 16 MB board expects `v1`; the board's identity keeps its measured capacity.
The flash request sends `allowLargerFlash` (and the older `allowDttV1On16Mb`)
only for such smaller-flash lanes.

Each release job keeps its redacted serial logs (the same `safe_line` filter as
PR runs, which keeps production and `main` health records) and the OSSM's
`rs485-guard.json` in `C:\rad-hil\release-worker\<release id>\`.
The published artifacts are downloaded by their immutable production URLs;
the worker does not require the optional Test Bench catalog credential.

**Cutover.** Install the worker first and confirm it polls. Roll out the hosted
admission workflows and default-branch reconciliation workflows in all four firmware repositories. Set the repository variable `FIRMWARE_VALIDATION_QUEUE_ENV=production` in all four, drain existing PR
hardware jobs, then enable physical admission by creating the marker below.
The worker checks the authenticated reservation and creates a fresh receipt.
The approved GitHub wrapper waits for its exact identity. A stale receipt or
a rerun with a different attempt cannot initiate a flash.

```powershell
New-Item -ItemType File 'C:\rad-hil\release-admission\enabled'
```

**Rollback.** Drain the current physical run and its reservation. Disable the
hosted waiters by clearing the repository variable in all four repositories, stop the idle scheduled task, then remove only the `enabled`
marker to restore the earlier PR-only behavior. Keep the private journal if a
lease is still open. Reconcile interrupted runs and their evidence before
retrying. Keep only one queue environment active per physical rig.

### Shared queue and fixture ownership

The dashboard orders PR and release jobs together. Critical jobs can move ahead
of queued jobs. A started run keeps its fixture until cleanup and no queue
reorder interrupts a flash. After queue cutover, the installed launcher
checks a fresh host receipt for the exact repository, run, attempt and SHA.
Manual bench sessions still yield to a reviewed PR. The release queue uses
the same fixture locks as the PR harness and protects a started release run
from watchdog preemption.

- **Fixture.** `rig_lock` records its holder in `<lock>.holder`. A pull-request
  run that finds a fixture taken leaves `<lock>.preempt` and waits up to five
  minutes. The rig watchdog (`C:\rad-hil\watchdog`, scheduled task
  `RAD Rig Watchdog`) terminates a preemptible manual holder. PR and release jobs mark
  their owned fixture as protected. Manual sessions must hold the fixture
  through `rig_lock` and expect to be stopped.
- **Guard.** When the OSSM guard is missing, latched or not handed back, the run
  first holds every OSSM in its ROM loader, so none sends pulses, then leaves
  `locks\ossm-guard.request`. The watchdog starts a missing guard. The guard
  checks that the motor is idle, healthy and between its measured stops, moves
  it slowly to mid-rail, hands it back and clears its latch. It refuses, and the
  run fails before any write, if those checks fail.
- **Record.** The fault that was cleared stays in the guard's `events.jsonl` and
  `latch.cleared.json`, in `results/rs485-guard.json`, and in the "Before
  flashing" line of the PR comment.

The firmware's own stop latch on a guarded bus is not stored, so the reset that
follows flashing clears it.

## Installation and runner migration

1. Review and commit the `hardware-run.yml` wrapper first. Callers and the runner
   group use its full immutable commit SHA from `reviewed-workflows.json`.
   Push that commit with the implementation branch before running its PR. This
   removes the bootstrap dependency on merging a new wrapper onto main first.
   Review wrapper updates and explicitly update the organization allowlist;
   changing a PR workflow alone never authorizes new host code. Follow repository
   manual-commit rules unless the owner explicitly authorizes committing/merging.
2. Run `install.ps1` from the reviewed checkout as the installation owner (or a host administrator). It installs
   the harness outside runner work directories, pins dependencies, records source
   hashes and prevents the runner service account from modifying installed code.
   Fixture JSON and provisioning secrets stay outside source and artifacts.
3. Create one organization runner group named `rad-firmware-validation`, with
   selected access to these six repositories, public repositories permitted,
   `restricted_to_workflows=true`, and exactly the six reviewed wrapper paths.
   `rollout.py prepare` creates reviewable REST payloads; it does not activate rules.
   See [GitHub runner group API](https://docs.github.com/en/rest/actions/self-hosted-runner-groups).
4. Drain and stop the existing Lockbox worker before moving its registration to
   the organization. Keep one worker only and retain `lkbx-rig-01`; add `rad-hil`.
   Register it in this group and run the service as the existing dedicated account.
   Never run it as administrator. Grant write access only to runner work/temp and
   lock directories; grant read/execute access to the harness and fixture.
5. Hardware jobs do not cancel in-progress flashing. One worker serializes jobs
   across repositories; OS locks additionally cover shared product families and
   the legacy Lockbox `C:\lkbx-hil\fixture.lock`, and a pull-request run takes
   them from a bench session (see above). Within a job both required
   variants flash and observe in parallel. A killed flash is a failure and needs
   explicit recovery; it never becomes a passing run on retry.
6. Run real PRs against both persistent branches for each product, plus a
   docs-only PR, which must skip the build and the rig yet pass both checks. Check complete device coverage, runtime merge SHA/image digest,
   180-second observation, network continuity, and the Lockbox migration regression.
   Fork builds remain on hosted runners; move reviewed changes to an internal
   branch before hardware validation can pass.
7. Only after those runs pass, activate the branch ruleset requiring
   `PR target policy`, `Build validation` and `Hardware validation`, integration
   ID 15368 (GitHub Actions), for main and staging. Do not require up-to-date
   branches: every firmware merge adds a version-bump commit, which would force
   another rig run on every open PR. Never bypass failed or skipped devices.

## Verification

Run `python -m unittest discover -s software/scripts/hil -p 'test_*.py'` from the checkout.
Tests exercise corrupted/stale bundles, wrong repository/chip/flash, overflowing
regions, missing variants/fixtures, cross-process locks, malformed serial records,
boot loops, stalled tasks, heartbeat gaps and network loss. These synthetic tests
do not replace full real PR checks or physical failure-injection verification.
