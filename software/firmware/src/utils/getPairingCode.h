#ifndef LOCKBOX_GETPAIRINGCODE_H
#define LOCKBOX_GETPAIRINGCODE_H

#include "Arduino.h"
#include "services/pairingCode.h"

static String getPairingCode() {
    // A permanent MAC is not a replacement for an issued expiring code.
    return currentPairingCode();
};

#endif  // LOCKBOX_GETPAIRINGCODE_H
