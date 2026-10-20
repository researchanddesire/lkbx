#ifndef LOCKBOX_GETPAIRINGCODE_H
#define LOCKBOX_GETPAIRINGCODE_H

#include "Arduino.h"
#include "WiFi.h"
#include "constants/strings.h"
#include "getEfuseMac.h"

static String getPairingCode() {
    String pairingCode;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        pairingCode = preferences.getString(Keys::Base::pairingCode, Str::Debug::ERROR_FALLBACK);
        preferences.end();

#ifdef VERSIONSTAGING
        pairingCode = Str::Debug::ERROR_FALLBACK;
#endif
        xSemaphoreGive(preferencesMutex);
    }

    // Normalize and validate. If missing or any variation of "null", fall back
    // to MAC (with colons removed).
    String normalized = pairingCode;
    normalized.trim();
    if (normalized.length() == 0 || normalized.equalsIgnoreCase(Str::Debug::ERROR_FALLBACK) ||
        normalized.equalsIgnoreCase(Str::Debug::NULL_STR) ||
        normalized.equalsIgnoreCase(Str::Debug::NULLPTR_STR) ||
        normalized.equalsIgnoreCase(Str::Debug::UNDEFINED_STR)) {
        String mac = getMacAddresss();
        mac.replace(":", "");
        return mac;
    }

    return normalized;
};

#endif  // LOCKBOX_GETPAIRINGCODE_H
