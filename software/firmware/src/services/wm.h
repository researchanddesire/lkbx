#ifndef LOCKBOX_WM_H
#define LOCKBOX_WM_H

#include "WiFiManager.h"

#include "http_worker.h"

extern WiFiManager wm;
void initWM();

// A Wi-Fi setup page was entered: open the setup portal for it. Called from
// the page's state machine action; it only leaves a note, because the portal
// is opened by a task that may wait for the MQTT client to stop.
void requestWifiPortal();

// Called often from the activity log task. Opens the portal for the newest
// page entry: stops the MQTT client first (the two do not fit in internal RAM
// together on R8 hardware), then creates the portal's task. If that task
// still cannot be created the client starts again and the page says that
// Wi-Fi setup could not be opened, instead of showing a QR code for a network
// that does not exist.
void serviceWifiPortal();

// Called often from the activity log task. Begins the Wi-Fi station again,
// with its saved credentials, when it has stayed down for 30 seconds
// (utils/stationKeeper.h). Never while a setup portal is open or wanted.
void serviceWifiStation();

#endif  // LOCKBOX_WM_H
