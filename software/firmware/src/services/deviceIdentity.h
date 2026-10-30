#ifndef LOCKBOX_DEVICEIDENTITY_H
#define LOCKBOX_DEVICEIDENTITY_H

#include <Arduino.h>

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

#endif  // LOCKBOX_DEVICEIDENTITY_H
