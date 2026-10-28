#pragma once
#include <Arduino.h>
#include "constants/dashboard.h"

#if defined(VERSIONDEV)
#ifndef LKBX_DEV_DASHBOARD_ORIGIN
#error Development pairing requires a local configured dashboard origin
#endif
#define LKBX_PAIRING_DASHBOARD_ORIGIN LKBX_DEV_DASHBOARD_ORIGIN
#else
#define LKBX_PAIRING_DASHBOARD_ORIGIN RAD_SERVER
#endif

inline String pairingDashboardUrl(const String& code) {
    return String(LKBX_PAIRING_DASHBOARD_ORIGIN) + "?lkbx=" + code;
}
