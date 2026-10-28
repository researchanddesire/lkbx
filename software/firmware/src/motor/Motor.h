#ifndef LOCKBOX_MOTOR_H
#define LOCKBOX_MOTOR_H

#include <Adafruit_INA219.h>

#include "Arduino.h"
#include "constants/Pins.h"
#include "services/settings.h"

class Motor {
    // A motor must have a ina219 sensor to measure the current

    // float array of 3 current measurements
    float current[3] = {0, 0, 0};
    bool isInverted = false;

  private:
    void watchCurrent();
    void waitForLock(); 
    void waitForUnlock();

    // Task handler for currentWatchTask
    TaskHandle_t currentWatchTaskHandler = nullptr;

  public:
    Motor();

    int lockId = 0;
    uint32_t lockCount = 0;
    uint32_t unlockCount = 0;
    Adafruit_INA219 *ina = nullptr;

    void calibrate(bool force = false);
    void syncToState();
    void lock(bool saveState = true);
    void unlock(bool saveState = true);
    static void clear();

    // is the device's state locked or unlocked.
    static bool isStateLocked();
    static bool isPaddleLocked();
    static bool isPaddleUnlocked();
    bool isCalibrated = false;
};

#endif  // LOCKBOX_MOTOR_H
