#ifndef LOCKBOX_GETID_H
#define LOCKBOX_GETID_H
#include "Arduino.h"
static auto getId() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    char macStr[13];
    snprintf(macStr, sizeof(macStr), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);

    return String(macStr);
};

#endif  // LOCKBOX_GETID_H
