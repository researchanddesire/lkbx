#ifndef LOCKBOX_LAST_WILL_H
#define LOCKBOX_LAST_WILL_H

#include <Arduino.h>

#include "FirmwareProvenance.h"
#include "constants/strings.h"
#include "services/deviceIdentity.h"
#include "utils/syncState.h"

// The last will the broker publishes for this connection if it drops. It is
// published on the MQTT client id, as it always was.
//
// THE FORMAT IS DECIDED HERE AND NOWHERE ELSE: this function chooses what a
// will says, and CommandGate::sealLastWill (lib/LockboxSecurity) how a sealed
// one is built. The dashboard side is rad-app's lib/lockbox-device/lastWill.
//
//   box with a command key (keyed v1 and v2):
//     {"v":4,"k":"will","ts","n","iv","ct","sig"}
//     signed  r4.{ts}.{clientId}.will.{n}.{iv}.{ct}   with the report MAC key
//     ct      AES-256-CBC, PKCS#7, report encryption key, over
//             {"id":"{n}","state":"disconnected","createdAt":0,
//              "firmwareProvenanceId":"..."}
//   box that has never signed:
//     {"id","state":"disconnected","createdAt":0,"firmwareProvenanceId"}
//
// Every call makes a new will: a new nonce, IV and timestamp when sealed, a new
// id when plain. Call it once per connection attempt
// (MQTT_EVENT_BEFORE_CONNECT), so the dashboard records each disconnect once.
//
// Returns an empty string when this connection must have no will
// (utils/lastWillPolicy.h): a box that signs never registers a plain one.
inline String buildLastWill(const String& clientId) {
    return lastWillForConnection(
        clientId.c_str(), firmware::provenance::currentTokenId().c_str(),
        createStateMessage(Str::Protocol::DISCONNECTED, false));
}

#endif  // LOCKBOX_LAST_WILL_H
