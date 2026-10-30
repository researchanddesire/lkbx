# Device Identity and Commands

How a Lockbox proves it is the enrolled device, and how it verifies commands sent to it over MQTT. The server decides when each rule is enforced, so firmware without these features keeps working until an operator retires it.

## Device secret

Each device holds a 32-byte secret, sent as unpadded base64url (43 characters).

| Source | When | Survives an NVS erase |
|--------|------|-----------------------|
| HMAC-SHA256 of `lkbx-device-secret-v1:<MAC>` with an eFuse key of purpose `HMAC_UP` | The unit was provisioned with an identity key | Yes |
| Random bytes stored in NVS (`identity/secret`) | Every other unit | No |

Firmware only reads eFuses and never burns one. Provisioning is a manual, per-device step with `software/scripts/provision_device_identity.py`, which refuses to run without an explicit permanent-action flag.

The secret is withheld while the running OTA image is still pending verification, so an image that could yet roll back never enrols the device.

| Request | How the secret is sent |
|---------|------------------------|
| `POST /api/lockbox/auth` | `deviceSecret` in the JSON body |
| `POST /api/lockbox/is-paired` | `deviceSecret` in the JSON body |
| `GET /api/lockbox/state/<mac>` | `x-lkbx-device-secret` header |

The secret goes only to the configured RAD server over verified TLS, and is never logged.

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
