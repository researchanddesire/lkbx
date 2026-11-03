#ifndef LOCKBOX_DEVICEIDENTITY_H
#define LOCKBOX_DEVICEIDENTITY_H

#include <Arduino.h>

#include "DeviceKey.h"

// The secret proving this is the enrolled device, or empty while it must not
// be sent: the running OTA image is unconfirmed or none can be read or created.
// Never log the returned value.
String deviceSecretForRequest();

// Applies the commandKey from a valid /auth 200 response. A response without
// one withdraws any stored key, so the server can turn command signing off.
void applyAuthCommandKey(const String& commandKey);

// Copies the command to process into `command`. Returns false, with nothing to
// process, when this device holds a key and the frame is not a valid envelope.
bool openCloudCommand(const char* mac, const char* frame, size_t size,
                      String& command);

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
