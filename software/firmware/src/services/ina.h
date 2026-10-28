#ifndef LOCKBOX_INA_H
#define LOCKBOX_INA_H

#include <Adafruit_INA219.h>
#include <Wire.h>

#include "constants/Pins.h"

extern Adafruit_INA219 ina;
extern bool inaReady;

void initINA();

#endif  // LOCKBOX_INA_H
