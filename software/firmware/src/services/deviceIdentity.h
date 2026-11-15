#ifndef LOCKBOX_DEVICEIDENTITY_H
#define LOCKBOX_DEVICEIDENTITY_H

#include <Arduino.h>

#include "CommandEnvelope.h"
#include "DeviceKey.h"

// The secret proving this is the enrolled device, or empty while it must not
// be sent: the running OTA image is unconfirmed or none can be read or created.
// Never log the returned value.
String deviceSecretForRequest();

// Applies the commandKey from a valid /auth 200 response. A new key replaces
// the stored one. A response without one changes nothing: once a box has held
// a key (or has a burned one) it never accepts plain commands again.
void applyAuthCommandKey(const String& commandKey);

// Copies the command received on `topic` into `command`. A "v2/<device id>"
// topic takes v3 envelopes bound to the device id, the MAC topic v2 envelopes
// bound to the MAC. Returns false, with nothing to process, when the frame is
// not a valid envelope for a box that holds a key or has a burned one, or was
// already accepted once (the replay ring survives reboots and sign-ins).
bool openCloudCommand(const char* topic, const char* frame, size_t size,
                      String& command);

// Seals a state report or activity log for `topic` (r3 with its kind and a
// nonce on a v2 topic, r2 on the MAC topic), so other broker clients can
// neither read nor forge it. A box that never held a key and has no burned
// one sends the report unchanged. Returns empty when a report must not be
// sent: a box that signs never reports in plain JSON, and nothing is sent
// before the stored key has loaded. Call only on a trusted clock.
String sealCloudReport(const char* topic, lockbox::security::ReportKind kind,
                       const String& report);

// The last will for a connection made as `clientId`: the v4 "will" seal for a
// box that holds a command key, `plainWill` unchanged for a box that has never
// signed, and empty when the connection must have no will (a box that signs
// never registers a plain one). Every call uses a new nonce, IV and timestamp.
// See utils/lastWill.h and utils/lastWillPolicy.h.
String lastWillForConnection(const char* clientId,
                             const char* firmwareProvenanceId,
                             const String& plainWill);

// The broker topic this boot speaks on: "v2/<device id>" after a v2 sign-in,
// the MAC otherwise. Logs go to "<topic>/log", provenance to "<topic>/firmware".
String cloudTopic();

// An image that could still roll back must not enroll the device or burn its
// key, or the older image it returns to would be locked out of its identity.
bool runningImageConfirmed();

// v2 identity: one dashboard-issued key in a read-protected eFuse block
// (purpose HMAC_UP). Firmware never reads it back; every value it sends is an
// HMAC computed inside the chip.
enum class DeviceKeyState { absent, present };
DeviceKeyState deviceKeyState();

// The v2 sign-in token, or empty when no key is burned or the HMAC fails.
// Never log the returned value.
String deviceTokenForRequest();

// The public v2 device id (lowercase hex of HMAC(key, "lkbx-device-id-v2"),
// 16 bytes), or empty when no key is burned or the HMAC fails. It names the
// box on the broker after a v2 sign-in; it proves nothing.
String deviceIdForRequest();

// Whether this boot signed in through v2. The pairing and state routes then
// use v2 as well; until it does, every route stays on v1.
bool signedInWithV2();
void noteSignIn(bool viaV2);

// Whether a key can be burned now: none is burned yet and a key block is free.
bool deviceKeySlotAvailable();

// Burns the key into the highest free key block, then proves the burn with the
// HMAC peripheral while the key is still in RAM. The caller must wipe `key`.
lockbox::security::DeviceKeyFailure burnDeviceKey(
    const uint8_t (&key)[lockbox::security::DEVICE_KEY_BYTES]);

#endif  // LOCKBOX_DEVICEIDENTITY_H
