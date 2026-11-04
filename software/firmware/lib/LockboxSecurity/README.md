# Device Identity and Commands

How a Lockbox proves it is the enrolled device, and how it verifies commands sent to it over MQTT. The server decides when each rule is enforced, so firmware without these features keeps working until an operator retires it.

## Device secret

Each device holds a 32-byte secret, sent as unpadded base64url (43 characters).

| Source | When | Survives an NVS erase |
|--------|------|-----------------------|
| HMAC-SHA256 of `lkbx-device-secret-v1:<MAC>` with an eFuse key of purpose `HMAC_UP` | The unit was provisioned with an identity key | Yes |
| Random bytes stored in NVS (`identity/secret`) | Every other unit | No |

Firmware 2.x burns one key itself through the v2 exchange below. `software/scripts/provision_device_identity.py` remains a manual factory step, which refuses to run without an explicit permanent-action flag; a unit it provisioned holds a key the dashboard does not know and signs in with v1 only.

The secret is withheld while the running OTA image is still pending verification, so an image that could yet roll back never enrols the device.

| Request | How the secret is sent |
|---------|------------------------|
| `POST /api/lockbox/auth` | `deviceSecret` in the JSON body |
| `POST /api/lockbox/is-paired` | `deviceSecret` in the JSON body |
| `GET /api/lockbox/state/<mac>` | `x-lkbx-device-secret` header |

The secret goes only to the configured RAD server over verified TLS, and is never logged.

## v2 device key (firmware 2.x)

A one-time exchange replaces the MAC as the device's identity. The dashboard
generates a random 256-bit key that has nothing to do with the MAC, sends it
exactly once, and keeps it only as AES-256 ciphertext. The device burns it into
the highest free eFuse key block with purpose `HMAC_UP`, which also sets read
and write protection. Firmware never reads the key back: every value it sends
is an HMAC that the chip computes internally.

| Step | Request | Outcome |
|------|---------|---------|
| Pre-auth, only while no key is burned | `POST /api/lockbox/v2/provision` with `mac`, `chip`, `md5`, `device`, `version`, the v1 `deviceSecret` and any previous `failureCode` | 201 `{deviceKey}`: burn now. 409 or 403: never ask again (stored in `identity/v2Refused`). 429: wait 24 hours (`identity/v2RetryAt`). Anything else: try on a later boot |
| Sign-in | `POST /api/lockbox/v2/auth` with `deviceToken` = base64url HMAC-SHA256 of `lkbx-device-token-v2` | The v1 response shape. 401 `unknown_device`, a 404 or an outage falls back to `/api/lockbox/auth` for that boot |
| Pairing status | `POST /api/lockbox/v2/is-paired` with `deviceToken` | 200 paired, 418 not paired |
| State | `GET /api/lockbox/v2/state` with header `x-lkbx-device-token` | The v1 state payload |

Every check that could refuse a burn runs before the key is requested: the
running image is confirmed, no key is burned, a key block is free, and the board
is on USB power or its battery reads at least 3.5 V. After the burn, the
firmware compares an HMAC of `lkbx-device-key-check-v2` from the peripheral with
one computed from the key still in RAM, then wipes the key.

A failure to store a received key is E-LOCK-999. During first setup it shows a
page that continues to the v1 sign-in; an onboarded device just signs in with
v1. Its code (993 invalid key, 994 no free block, 995 write failed, 996 verify
failed, 997 burn refused in a development build) goes to the dashboard with
the next key request, which may then replace an unconfirmed key for the same v1
secret, up to three times.

The burned key also changes the v1 secret, which derives from the same eFuse
key. The dashboard re-enrolls the device to that secret at its first v2 sign-in
and marks it hardware-bound, so an older image on the same chip keeps its
identity and no v1 request can ever replace it.

`development-r8-virtual-efuse` runs the whole exchange against simulated eFuses
kept in the `efuse_em` flash partition; erase that partition to start over.
`test/test_device_key` holds the vector shared with the dashboard.

## Sealed reports

A device holding a command key seals its state reports and activity logs in
the same envelope shape as commands, with keys derived from the command key by
the labels `lkbx-rpt-mac-v2` and `lkbx-rpt-enc-v2`, and a signature over
`r2.{ts}.{MAC}.{iv}.{ct}`. Other broker clients can neither read nor forge
them, and a captured report never opens as a command (its signature fails).
Each LOCKED retry is sealed afresh from the stored plain report. The dashboard
opens them in Postgres (`lockbox_device_report_open`), accepts plain reports
from firmware without a key, and refuses plain reports for a hardware-bound
device, which always seals. The retained `<MAC>/firmware` provenance and the
last will stay plain; neither carries session data.

## Physical access

These measures protect the cloud channel, not a box in someone's hands. The
chip ships without secure boot, flash encryption or a restricted download mode,
so anyone with a USB-C cable can read the NVS (Wi-Fi password, session, and the
command key) and flash other firmware; the v2 key itself stays unreadable
because its block is read-protected, but other firmware on that unit can still
use it through the HMAC peripheral. Every key is per device, so this exposes
only the box in hand.

Closing it is a factory change, not a firmware one, because every step is a
permanent eFuse burn that also changes how technicians flash units:

1. Secure Boot v2 (signed bootloader and app; the RSA key digest goes in a free
   key block, which the v2 device key, in the highest block, leaves free).
2. Flash encryption in release mode, with NVS encryption keyed by the HMAC
   peripheral, so the command key and Wi-Fi password are no longer readable.
3. `ENABLE_SECURITY_DOWNLOAD` (no flash reads over USB), and `DIS_USB_JTAG`
   plus `DIS_PAD_JTAG`.

Prove the sequence on a sacrificial unit, move the Build PC to signed images,
and only then burn production units. Never run it on a bench unit someone still
needs to reflash freely.

## Authentication responses

`POST /api/lockbox/auth` may return `commandKey`, 32 bytes as 43-character base64url.

| Response | Device behaviour |
|----------|------------------|
| 200 with `commandKey` | Stores the key in NVS (`identity/cmdKey`) and requires signed commands |
| 200 without `commandKey` | Erases any stored key and accepts plain commands |
| 401 | Keeps its state and retries every 60 seconds |
| Any other failure | Keeps its state and retries as before |

A failed authentication only means the device stays off the broker. It never locks, unlocks or discards a session; timers and local controls keep working offline.

## Command envelope

A device holding a key accepts only this envelope on its command topic:

```json
{"v":2,"ts":1788800000000,"iv":"<base64url>","ct":"<base64url>","sig":"<base64url>"}
```

- `macKey` is HMAC-SHA256 of `lkbx-cmd-mac-v2` with the command key; `encKey` is HMAC-SHA256 of `lkbx-cmd-enc-v2` with the command key.
- `ct` is AES-256-CBC with PKCS#7 padding under `encKey`, using the 16-byte `iv`. Its plaintext is the same JSON command a device without a key receives.
- `sig` is HMAC-SHA256 under `macKey` of the text `v2.{ts}.{MAC}.{iv}.{ct}`, where `MAC` is uppercase colon-separated and `iv` and `ct` are their base64url text.

The signature is verified in constant time before anything is decrypted. A command is then accepted only if `ts + 30000` is at least the highest timestamp seen (persisted in `identity/cmdLastTs`), and its signature is not among the last 8 accepted. The window allows for messages delivered out of order.

A device without a key processes plain JSON unchanged.

Devices drop retained messages on the command topic in both modes. The only commands are `sync` and `reboot`.
