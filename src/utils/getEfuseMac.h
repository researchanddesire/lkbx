#ifndef SOFTWARE_GETEFUSMAC_H
#define SOFTWARE_GETEFUSMAC_H

#include "Arduino.h"

static String getMacAddresss() {
#ifdef VERSIONDEV
    return String(LKBX_DEV_DEVICE_ID);
#endif
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

#endif  // SOFTWARE_GETEFUSMAC_H
