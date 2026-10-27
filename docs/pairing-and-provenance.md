# Pairing refresh and bounded provenance

The onboarding and settings pairing screens use an authenticated, RAM-only code
lease. `pairingCodeExpiresAt` is ISO UTC from `/api/lockbox/auth`. The device
accepts a six-character uppercase code and stops displaying it ten seconds before
its authoritative expiry or five-minute monotonic age cap. A failed response or
clock rollback cannot extend that lease. Older servers that omit expiry retain
a finite five-minute RAM lease; no persisted value survives reboot as a valid QR.
Missing or expired codes never fall back to a permanent MAC identifier.

One worker refreshes only while either pairing screen is active. HTTP calls have
three-second total deadlines and authentication retry is limited to once per
five seconds. Refresh never reconfigures the broker or restarts the device.
Screen updates are state-machine events, so a late response after Skip cannot
redraw or advance an unrelated screen. Pairing still requires the server's
authenticated device/status and ownership checks.

Release QR links use the profile's configured RAD origin. Development links use
`python scripts/dev_services.py configure --dashboard-origin http://192.168.1.17:3000`.
That option accepts only explicit private/loopback IPv4 HTTP(S) origins, without
credentials, paths, queries or fragments. Default is the selected LAN IP on port
3000; the device API remains on its independent pinned HTTPS origin.

## BLE provenance v2

Read `device.firmwareProvenance` without arguments for bounded metadata. Existing
metadata keys remain; `compactJws` is empty and the additive fields are `v: 2`,
`proofBytes` and `proofPageBytes: 64`. Summary metadata alone is not proof of an
official image. The existing full-token signature and runtime-image claim
verification still determines `origin`.

To retrieve the signed proof, retain the summary's `provenanceId` and send
`args: {view: "proof", provenanceId, offset: 0}`. Responses contain `encoding:
"base64url"`, `chunk`, `offset`, `nextOffset` and `totalBytes`. Each chunk encodes
at most 64 raw bytes. Validate the reference, offset progression and total on
every response, decode and reassemble the exact bytes, then verify the complete
signature and matching runtime claims. A changed reference fails with
`stale_provenance`; restart from a fresh summary. Proofs are bounded to 4096 bytes.

Explicit `view: "full"` with the matching reference preserves the earlier full
object for small proofs. Larger objects fail with `proof_requires_paging`.
There is no transport-size increase, proof truncation, trust bypass or NVS edit.

## Host regression checks

- `python scripts/test_pairing_refresh.py` tests the real lease/service/polling
  code and exact display transition fragment in all three origin profiles.
- `python test/security_firmware_provenance/test_response.py` exercises the
  unmodified signature verifier with a public signed vector, full proof
  reassembly, invalid/stale cases and the actual pinned RAD BLE sender across
  every LKBX state-name pair.
- `python scripts/test_http_worker_security.py` retains authenticated clock,
  configured-origin and server-connectivity checks in both security modes.

Lease/service/polling tests use ASan/UBSan. Clang excludes only the unmodified
Boost.SML implementation from UBSan because its zero-storage empty functors
trigger object-size instrumentation; ASan remains enabled. Host checks do not
replace required embedded compilation or physical fixture validation.
