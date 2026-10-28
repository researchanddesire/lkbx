#include <Arduino.h>
#include <services/buttons.h>
#include <services/idle.h>

#include <cmath>

#include "constants/Pins.h"
#include "motor/Motor.h"
#include "services/board.h"
#include "services/display.h"
#include "services/encoder.h"
#include "services/ina.h"
#include "services/magnetics.h"
#include "services/mqtt.h"
#include "state/LKBX.h"
#include "test_utils.h"

// ── Peripheral tests (no state machine) ─────────────────────────────────

void test_display_init() {
    initDisplay();
    TEST_ASSERT_NOT_NULL_MESSAGE(displayMutex,
                                 "Display mutex should be created");
    TEST_MESSAGE("initDisplay() OK");
}

void test_pin_config() {
    initPinConfig();
    initMotorGPIO();

    TEST_ASSERT_TRUE_MESSAGE(GPIO_IS_VALID_OUTPUT_GPIO(Pins::MOTOR::A),
                             "Motor A pin should be valid output GPIO");
    TEST_MESSAGE("initPinConfig() + initMotorGPIO() OK");
}

void test_ina219_init_and_reading() {
    initINA();
    TEST_ASSERT_TRUE_MESSAGE(inaReady, "INA219 should be initialized");

    float current = ina.getCurrent_mA();
    char msg[128];
    snprintf(msg, sizeof(msg), "INA219 idle current: %.2f mA", current);
    TEST_MESSAGE(msg);

    TEST_ASSERT_FALSE_MESSAGE(isnan(current),
                              "INA219 should return a valid number");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(
        500.0f, 0.0f, current,
        "Idle motor current should be near zero (within 500mA)");
}

void test_magnetics_init() {
    initMagnetics();

    bool tmr1 = digitalRead(Pins::Magnetics::tmr1) == LOW;
    bool tmr2 = digitalRead(Pins::Magnetics::tmr2) == LOW;
    bool tmr3 = digitalRead(Pins::Magnetics::tmr3) == LOW;

    char msg[128];
    snprintf(msg, sizeof(msg), "TMR states: tmr1=%d tmr2=%d tmr3=%d", tmr1,
             tmr2, tmr3);
    TEST_MESSAGE(msg);

    int raw1 = digitalRead(Pins::Magnetics::tmr1);
    int raw2 = digitalRead(Pins::Magnetics::tmr2);
    int raw3 = digitalRead(Pins::Magnetics::tmr3);
    TEST_ASSERT_TRUE_MESSAGE(raw1 == HIGH || raw1 == LOW,
                             "TMR1 should read HIGH or LOW");
    TEST_ASSERT_TRUE_MESSAGE(raw2 == HIGH || raw2 == LOW,
                             "TMR2 should read HIGH or LOW");
    TEST_ASSERT_TRUE_MESSAGE(raw3 == HIGH || raw3 == LOW,
                             "TMR3 should read HIGH or LOW");

    bool paddleLocked = Motor::isPaddleLocked();
    int activeCount = (int)tmr1 + (int)tmr2 + (int)tmr3;
    char msg2[128];
    snprintf(msg2, sizeof(msg2),
             "TMR active (LOW) count: %d, isPaddleLocked: %d", activeCount,
             paddleLocked);
    TEST_MESSAGE(msg2);

    if (activeCount >= 2) {
        TEST_ASSERT_TRUE_MESSAGE(paddleLocked,
                                 "2+ active TMRs should mean paddle locked");
    } else {
        TEST_ASSERT_FALSE_MESSAGE(paddleLocked,
                                  "<2 active TMRs should mean paddle unlocked");
    }
}

// Drive a specific pin-A + direction combo for `ms`, then clear all motor pins.
static void driveRaw(int pinA, int pinB, bool aHigh, uint32_t ms) {
    digitalWrite(pinA, aHigh ? HIGH : LOW);
    digitalWrite(38, aHigh ? HIGH : LOW);
    digitalWrite(pinB, aHigh ? LOW : HIGH);
    digitalWrite(3, aHigh ? LOW : HIGH);
    vTaskDelay(ms / portTICK_PERIOD_MS);
    digitalWrite(pinA, LOW);
    digitalWrite(38, LOW);
    digitalWrite(pinB, LOW);
    digitalWrite(3, LOW);
    vTaskDelay(200 / portTICK_PERIOD_MS);
}

void test_motor_polarity_calibration() {
    char msg[128];
    const int PIN_B = 45;
    const int PIN_A_OPTIONS[] = {46, 12};

    bool found = false;
    bool btnInv = false;
    bool mtrInv = false;

    for (int pinA : PIN_A_OPTIONS) {
        pinMode(pinA, OUTPUT);
        pinMode(38, OUTPUT);
        pinMode(PIN_B, OUTPUT);
        pinMode(3, OUTPUT);

        // Direction 1: A=LOW B=HIGH  (matches Motor::lock when not inverted)
        driveRaw(pinA, PIN_B, false, 3000);
        bool lockedDir1 = Motor::isPaddleLocked();

        // Direction 2: A=HIGH B=LOW  (matches Motor::lock when inverted)
        driveRaw(pinA, PIN_B, true, 3000);
        bool lockedDir2 = Motor::isPaddleLocked();

        snprintf(msg, sizeof(msg), "pinA=%d  dir1_locked=%d  dir2_locked=%d",
                 pinA, lockedDir1, lockedDir2);
        TEST_MESSAGE(msg);

        if (lockedDir1 == lockedDir2) continue;

        found = true;
        btnInv = (pinA == 12);
        mtrInv = lockedDir2;

        // Leave paddle unlocked.
        if (Motor::isPaddleLocked()) {
            driveRaw(pinA, PIN_B, mtrInv ? false : true, 3000);
        }
        break;
    }

    TEST_ASSERT_TRUE_MESSAGE(
        found,
        "No pin/polarity combo moved the paddle. Check motor + TMR wiring.");

    // Save to NVS and update runtime pins.
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base, false);
        preferences.putBool(Keys::Base::isButtonInverted, btnInv);
        preferences.putBool(Keys::Base::isMotorInverted, mtrInv);
        preferences.putBool(Keys::Base::isMotorCalibrated, true);
        preferences.putBool(Keys::Base::isButtonCalibrated, true);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }

    Pins::MOTOR::A = btnInv ? 12 : 46;
    Pins::Buttons::buttonEnter = btnInv ? 46 : 12;
    initMotorGPIO();

    snprintf(msg, sizeof(msg), "Calibration saved: btnInv=%d mtrInv=%d pinA=%d",
             btnInv, mtrInv, Pins::MOTOR::A);
    TEST_MESSAGE(msg);

    // Verify with the Motor API.
    Motor motor;
    motor.ina = &ina;

    motor.lock(false);
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    Motor::clear();
    vTaskDelay(200 / portTICK_PERIOD_MS);
    bool lockOk = Motor::isPaddleLocked();

    motor.unlock(false);
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    Motor::clear();
    vTaskDelay(200 / portTICK_PERIOD_MS);
    bool unlockOk = Motor::isPaddleUnlocked();

    snprintf(msg, sizeof(msg), "Motor API verify: lock=%d unlock=%d", lockOk,
             unlockOk);
    TEST_MESSAGE(msg);

    TEST_ASSERT_TRUE_MESSAGE(lockOk, "Motor::lock() should lock the paddle");
    TEST_ASSERT_TRUE_MESSAGE(unlockOk,
                             "Motor::unlock() should unlock the paddle");
}

void test_board_and_motor_init() {
    initBoard();

    motor = new Motor();
    motor->ina = &ina;
    delay(500);

    initButtons();
    initIdleTask();

    TEST_ASSERT_NOT_NULL_MESSAGE(motor, "Motor should be initialized");
    TEST_ASSERT_NOT_NULL_MESSAGE(displayMutex, "Display mutex should exist");
    TEST_MESSAGE("Board and motor initialized (SM deferred)");
}

// Called from main.cpp AFTER WiFi + pairing are confirmed.
// This avoids the race condition where SM auth runs before pairing.
void startStateMachineForTests() {
    if (lkbx) {
        ESP_LOGW("TEST", "SM already started, skipping");
        return;
    }

    lkbx = new LKBX(display, rotaryEncoder, *motor, mqttClient);
    lkbx->initDelayTask();

    TEST_ASSERT_NOT_NULL(lkbx);
    TEST_ASSERT_NOT_NULL(lkbx->sm.get());

    vTaskDelay(500 / portTICK_PERIOD_MS);

    String state = currentState();
    char msg[128];
    snprintf(msg, sizeof(msg), "SM started, state: %s", state.c_str());
    TEST_MESSAGE(msg);
}

void test_motor_lock_drives_current() {
    TEST_ASSERT_TRUE_MESSAGE(inaReady, "INA219 must be initialized");
    TEST_ASSERT_NOT_NULL_MESSAGE(motor, "Motor must be initialized");

    motor->lock(false);
    vTaskDelay(200 / portTICK_PERIOD_MS);

    float current = ina.getCurrent_mA();
    char msg[128];
    snprintf(msg, sizeof(msg), "Motor lock current: %.2f mA", current);
    TEST_MESSAGE(msg);

    Motor::clear();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    TEST_ASSERT_TRUE_MESSAGE(
        fabs(current) > 5.0f,
        "Motor should draw measurable current during lock actuation");
}

void test_motor_unlock_drives_current() {
    TEST_ASSERT_TRUE_MESSAGE(inaReady, "INA219 must be initialized");
    TEST_ASSERT_NOT_NULL_MESSAGE(motor, "Motor must be initialized");

    if (!Motor::isPaddleLocked()) {
        motor->lock(false);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        Motor::clear();
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    motor->unlock(false);
    vTaskDelay(200 / portTICK_PERIOD_MS);

    float current = ina.getCurrent_mA();
    char msg[128];
    snprintf(msg, sizeof(msg), "Motor unlock current: %.2f mA", current);
    TEST_MESSAGE(msg);

    Motor::clear();
    vTaskDelay(100 / portTICK_PERIOD_MS);

    TEST_ASSERT_TRUE_MESSAGE(
        fabs(current) >= 1.0f,
        "Motor should draw measurable current during unlock actuation");
}

void test_motor_clear_stops_drive() {
    TEST_ASSERT_NOT_NULL_MESSAGE(motor, "Motor must be initialized");

    motor->lock(false);
    vTaskDelay(100 / portTICK_PERIOD_MS);
    Motor::clear();
    vTaskDelay(200 / portTICK_PERIOD_MS);

    int pinA = digitalRead(Pins::MOTOR::A);
    int pinB = digitalRead(Pins::MOTOR::B);
    int pin38 = digitalRead(38);
    int pin3 = digitalRead(3);

    char msg[128];
    snprintf(msg, sizeof(msg), "After clear: A=%d B=%d pin38=%d pin3=%d", pinA,
             pinB, pin38, pin3);
    TEST_MESSAGE(msg);

    TEST_ASSERT_EQUAL_INT_MESSAGE(LOW, pinA,
                                  "Motor A should be LOW after clear");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LOW, pinB,
                                  "Motor B should be LOW after clear");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LOW, pin38,
                                  "Pin 38 should be LOW after clear");
    TEST_ASSERT_EQUAL_INT_MESSAGE(LOW, pin3, "Pin 3 should be LOW after clear");

    if (inaReady) {
        float current = ina.getCurrent_mA();
        char msg2[128];
        snprintf(msg2, sizeof(msg2), "Post-clear current: %.2f mA", current);
        TEST_MESSAGE(msg2);
    }
}

void test_motor_lock_unlock_cycle_with_tmr() {
    TEST_ASSERT_NOT_NULL_MESSAGE(motor, "Motor must be initialized");

    bool initialPaddle = Motor::isPaddleLocked();
    char msg[256];
    snprintf(msg, sizeof(msg), "Initial paddle state: %s",
             initialPaddle ? "LOCKED" : "UNLOCKED");
    TEST_MESSAGE(msg);

    TEST_MESSAGE("--- Locking motor ---");
    motor->lock(false);
    vTaskDelay(2500 / portTICK_PERIOD_MS);
    Motor::clear();
    vTaskDelay(200 / portTICK_PERIOD_MS);

    bool tmr1Lock = digitalRead(Pins::Magnetics::tmr1) == LOW;
    bool tmr2Lock = digitalRead(Pins::Magnetics::tmr2) == LOW;
    bool tmr3Lock = digitalRead(Pins::Magnetics::tmr3) == LOW;
    bool paddleAfterLock = Motor::isPaddleLocked();

    snprintf(msg, sizeof(msg),
             "After lock: tmr1=%d tmr2=%d tmr3=%d isPaddleLocked=%d", tmr1Lock,
             tmr2Lock, tmr3Lock, paddleAfterLock);
    TEST_MESSAGE(msg);

    TEST_ASSERT_TRUE_MESSAGE(paddleAfterLock,
                             "Paddle should be locked after motor lock");

    int lockActiveCount = (int)tmr1Lock + (int)tmr2Lock + (int)tmr3Lock;
    TEST_ASSERT_TRUE_MESSAGE(
        lockActiveCount >= 2,
        "At least 2 of 3 TMR sensors should be active after lock");

    TEST_MESSAGE("--- Unlocking motor ---");
    motor->unlock(false);
    vTaskDelay(2500 / portTICK_PERIOD_MS);
    Motor::clear();
    vTaskDelay(200 / portTICK_PERIOD_MS);

    bool tmr1Unlock = digitalRead(Pins::Magnetics::tmr1) == LOW;
    bool tmr2Unlock = digitalRead(Pins::Magnetics::tmr2) == LOW;
    bool tmr3Unlock = digitalRead(Pins::Magnetics::tmr3) == LOW;
    bool paddleAfterUnlock = Motor::isPaddleUnlocked();

    snprintf(msg, sizeof(msg),
             "After unlock: tmr1=%d tmr2=%d tmr3=%d isPaddleUnlocked=%d",
             tmr1Unlock, tmr2Unlock, tmr3Unlock, paddleAfterUnlock);
    TEST_MESSAGE(msg);

    TEST_ASSERT_TRUE_MESSAGE(paddleAfterUnlock,
                             "Paddle should be unlocked after motor unlock");

    int unlockActiveCount = (int)tmr1Unlock + (int)tmr2Unlock + (int)tmr3Unlock;
    TEST_ASSERT_TRUE_MESSAGE(
        unlockActiveCount < 2,
        "Fewer than 2 TMR sensors should be active after unlock");
}

// ── Registration ────────────────────────────────────────────────────────

void register_peripheral_tests() {
    RUN_TEST(test_display_init);
    RUN_TEST(test_pin_config);
    RUN_TEST(test_ina219_init_and_reading);
    RUN_TEST(test_magnetics_init);
    RUN_TEST(test_motor_polarity_calibration);
}

void register_board_tests() {
    RUN_TEST(test_board_and_motor_init);
    RUN_TEST(test_motor_lock_drives_current);
    RUN_TEST(test_motor_unlock_drives_current);
    RUN_TEST(test_motor_clear_stops_drive);
    RUN_TEST(test_motor_lock_unlock_cycle_with_tmr);
}
