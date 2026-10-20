#pragma once

#include <RadBle.h>

extern radble::Server radBleServer;

bool initRadBle(NimBLEServer* server);
bool startBle();
bool stopBle();
bool isBlePaused();
