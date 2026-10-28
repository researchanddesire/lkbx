#ifndef LOCKBOX_WM_H
#define LOCKBOX_WM_H

#include "WiFiManager.h"

#include "http_worker.h"

extern WiFiManager wm;
void initWM();

void processWifiTask(void *pvParameters);

#endif  // LOCKBOX_WM_H
