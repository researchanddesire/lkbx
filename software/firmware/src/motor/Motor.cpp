#include "Motor.h"

#include <esp_heap_caps.h>

#include "constants/LogTags.h"
#include "constants/PerferenceKeys.h"
#include "constants/strings.h"
#include "tasks/TaskParams.h"

// The end of a movement: the motor stays powered for up to a second more, so
// the paddle is pressed home against its stop, and is switched off at the
// current limit or when the second is over. A newer movement (lockId) takes
// the motor over.
void Motor::runCurrentWatch() {
    const int watchedLockId = lockId;

    current[0] = 0;
    current[1] = 0;
    current[2] = 0;

    int timeElapsed = 0;
    bool hasHitCurrentLimit = false;
    float avgCurrent = 0;
    while (timeElapsed < 1000 && watchedLockId == lockId) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        timeElapsed += 50;

        if (hasHitCurrentLimit) {
            continue;
        }

        float measured = ina->getCurrent_mA();
        current[0] = current[1];
        current[1] = current[2];
        current[2] = measured;

        avgCurrent = (current[0] + current[1] + current[2]) / 3;

        ESP_LOGD("Motor", "Current: %f", avgCurrent);

        if (avgCurrent > 50) {
            hasHitCurrentLimit = true;
            Motor::clear();
            continue;
        }
    }

    if (!hasHitCurrentLimit && watchedLockId == lockId) {
        Motor::clear();
    }
}

void Motor::watchCurrent() {
    // The watch of the movement before this one ends within 50 ms of seeing
    // this one, without touching the motor. Returning while it was still
    // there left this movement with no watch at all, and the motor powered.
    for (int waitedMs = 0;
         TaskHandles::currentWatch != nullptr && waitedMs < 200;
         waitedMs += 10) {
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (TaskHandles::currentWatch != nullptr) {
        ESP_LOGW(TASKDEBUG_TAG,
                 "currentWatch task still running; finishing the movement "
                 "on this task");
        runCurrentWatch();
        return;
    }

    ESP_LOGD(TASKDEBUG_TAG, "Creating current watch task");
    const BaseType_t created = xTaskCreatePinnedToCore(
        [](void *pvParameters) {
            static_cast<Motor *>(pvParameters)->runCurrentWatch();
            TaskHandles::currentWatch = nullptr;
            vTaskDelete(nullptr);
        },
        Tasks::currentWatch.name, Tasks::currentWatch.stackSize, this,
        Tasks::currentWatch.priority, &TaskHandles::currentWatch,
        Tasks::currentWatch.core);
    if (created != pdPASS) {
        // No memory for the task's stack. The same watch runs here instead,
        // on the task that moved the motor: the paddle is still pressed home
        // and the motor is still switched off, at the current limit or after
        // a second. Cutting the power at once, as this did before, left the
        // paddle wherever the position sensors first saw it, which can be
        // short of its stop. The caller waits up to a second longer.
        TaskHandles::currentWatch = nullptr;
        ESP_LOGE(TASKDEBUG_TAG,
                 "Could not start the current watch (largest free internal "
                 "block %u bytes, %u bytes free); finishing the movement on "
                 "this task",
                 static_cast<unsigned>(
                     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
        runCurrentWatch();
    }
}
void Motor::waitForLock() {
    // Wait for the motor to lock the box.
    int timeElapsed = 0;
    while (timeElapsed < 2000) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        timeElapsed += 50;
        if (isPaddleLocked()) {
            break;
        }
    }
}
void Motor::waitForUnlock() {
    // Wait for the motor to lock the box.
    int timeElapsed = 0;
    while (timeElapsed < 2000) {
        vTaskDelay(50 / portTICK_PERIOD_MS);
        timeElapsed += 50;
        if (isPaddleUnlocked()) {
            break;
        }
    }
}

void Motor::lock(bool saveState) {
    lockId++;
    lockCount++;

    if (isInverted) {
        digitalWrite(Pins::MOTOR::A, HIGH);
        digitalWrite(38, HIGH);
        digitalWrite(Pins::MOTOR::B, LOW);
        digitalWrite(3, LOW);
    } else {
        digitalWrite(Pins::MOTOR::A, LOW);
        digitalWrite(38, LOW);
        digitalWrite(Pins::MOTOR::B, HIGH);
        digitalWrite(3, HIGH);
    }

    if (saveState == true &&
        xSemaphoreTake(preferencesMutex, portMAX_DELAY) == pdTRUE) {
        preferences.begin(Keys::base, false);
        // A lock that is only now being stored has not passed its check yet,
        // whatever the lock before it left behind (utils/heldLock.h).
        if (!preferences.getBool(Keys::Base::isLocked, false) &&
            preferences.getBool(Keys::Base::lockHeld, false)) {
            preferences.putBool(Keys::Base::lockHeld, false);
        }
        preferences.putBool(Keys::Base::isLocked, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    waitForLock();
    watchCurrent();
}

void Motor::unlock(bool saveState) {
    lockId++;
    unlockCount++;

    if (isInverted) {
        digitalWrite(Pins::MOTOR::A, LOW);
        digitalWrite(38, LOW);

        digitalWrite(Pins::MOTOR::B, HIGH);
        digitalWrite(3, HIGH);
    } else {
        digitalWrite(Pins::MOTOR::A, HIGH);
        digitalWrite(38, HIGH);
        digitalWrite(Pins::MOTOR::B, LOW);
        digitalWrite(3, LOW);
    }

    // The paddle has been told to open, so the box no longer holds a
    // confirmed lock, whether or not the stored lock ends with it (a break
    // and an error page keep it). See utils/heldLock.h.
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) == pdTRUE) {
        preferences.begin(Keys::base, false);
        if (preferences.getBool(Keys::Base::lockHeld, false)) {
            preferences.putBool(Keys::Base::lockHeld, false);
        }
        if (saveState == true) {
            preferences.putBool(Keys::Base::isLocked, false);
        }
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    waitForUnlock();
    watchCurrent();
}

void Motor::clear() {
    digitalWrite(Pins::MOTOR::A, LOW);
    digitalWrite(38, LOW);
    digitalWrite(Pins::MOTOR::B, LOW);
    digitalWrite(3, LOW);
}

bool Motor::isStateLocked() {
    // Check if the current stored preference state is LOCKED.
    bool isStateLocked = false;
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) == pdTRUE) {
        isStateLocked = preferences.getLockedState();
        xSemaphoreGive(preferencesMutex);
    }
    return isStateLocked;
}

bool Motor::isPaddleLocked() {
    // is the paddle in the correct position to lock the box?
    bool tmr1 = digitalRead(Pins::Magnetics::tmr1) == LOW;
    bool tmr2 = digitalRead(Pins::Magnetics::tmr2) == LOW;
    bool tmr3 = digitalRead(Pins::Magnetics::tmr3) == LOW;

    // Any two of the three TMRs must be LOW to lock the paddle.
    return (tmr1 && tmr2 && tmr3) || (tmr1 && tmr2) || (tmr1 && tmr3) ||
           (tmr2 && tmr3);
}
bool Motor::isPaddleUnlocked() {
    // is the paddle in the correct position to unlock the box?
    return !isPaddleLocked();
}
void Motor::syncToState() {
    // Sync the motor to the current state of the box.
    if (isStateLocked()) {
        lock(false);
    } else {
        unlock(false);
    }
}

void Motor::calibrate(bool force) {
    // update the calibration state from memory.
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) == pdTRUE) {
        preferences.begin(Keys::base);
        isCalibrated =
            preferences.getBool(Keys::Base::isMotorCalibrated, false);
        isInverted = preferences.getBool(Keys::Base::isMotorInverted, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    if (isCalibrated && !force) {
        esp_restart();
        return;
    }

    // TODO: Say Hello to server and store ID.

    // TODO: Press Enter to calibrate button vs Motor Pins.

    // try to lock the device.
    ESP_LOGD(MOTOR_TAG, "LOCK #1");
    lock(false);
    delay(3000);

    // check if the paddle is locked.
    bool isLocked = isPaddleLocked();

    ESP_LOGD(MOTOR_TAG, "UNLOCK #1");
    unlock(false);
    delay(3000);
    bool isUnlocked = !isPaddleLocked();

    if (isLocked && isUnlocked) {
        // the motor is not inverted.
        isInverted = false;
        isCalibrated = true;
    } else if (!isLocked && !isUnlocked) {
        // the motor is inverted.
        isInverted = true;
        isCalibrated = true;
    } else {
        isCalibrated = false;
    }

    ESP_LOGD(MOTOR_TAG, "isInverted: %d", isInverted);

    // TODO: Check back plate Off
    // TODO: Check back plate On

    // TODO: Check Lock actually Locks
    // TODO: Check Unlock actually Unlocks

    // TODO: Ready to ship?
    // TODO: Yes -> Erase Alex's WiFi Details.

    ESP_LOGD(MOTOR_TAG, "Calibrating motor");
    ESP_LOGD(MOTOR_TAG, "isCalibrated: %d", isCalibrated);
    ESP_LOGD(MOTOR_TAG, "isInverted: %d", isInverted);

    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isMotorCalibrated, isCalibrated);
        preferences.putBool(Keys::Base::isMotorInverted, isInverted);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    syncToState();

    // and restart device.
    esp_restart();
}

Motor::Motor() {
    // the motor is calibrated correctly.
    if (xSemaphoreTake(preferencesMutex, portMAX_DELAY) == pdTRUE) {
        preferences.begin(Keys::base);
        isCalibrated =
            preferences.getBool(Keys::Base::isMotorCalibrated, false);
        isInverted = preferences.getBool(Keys::Base::isMotorInverted, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    ESP_LOGD(MOTOR_TAG, "Motor is %s", isInverted ? Str::Debug::INVERTED : Str::Debug::NOT_INVERTED);
    ESP_LOGD(MOTOR_TAG, "Motor is %s",
             isCalibrated ? Str::Debug::CALIBRATED : Str::Debug::NOT_CALIBRATED);
}
