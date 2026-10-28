#include <unity.h>
#include "sleep_fakes.h"
#include "../../src/services/sleep.h"
#include "../../src/services/lastInteraction.h"
#include "../../src/constants/Pins.h"
#include "../../src/utils/idleTarget.h"

void test_power_blocks_sleep_before_disconnect_or_panel_shutdown() {
    sleepFake.power = true;
    bool disconnected = false;
    enterSleep(0, [&]() { disconnected = true; });
    TEST_ASSERT_FALSE(disconnected);
    TEST_ASSERT_EQUAL(0, sleepFake.deepSleeps);
    TEST_ASSERT_TRUE(sleepFake.panelCommands.empty());
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP, idleState);
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP,
                      idleTargetFromDelta(millis() - lastInteraction, true));
}

void test_usb_host_blocks_sleep_even_without_power_sense() {
    sleepFake.usb = true;
    enterSleep(60000);
    TEST_ASSERT_EQUAL(0, sleepFake.deepSleeps);
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP, idleState);
}

void test_battery_sleep_preserves_button_timer_and_adds_power_wake() {
    Pins::Buttons::buttonEnter = 12;
    enterSleep(45000);
    TEST_ASSERT_EQUAL(1, sleepFake.deepSleeps);
    TEST_ASSERT_EQUAL(12, sleepFake.button);
    TEST_ASSERT_EQUAL(0, sleepFake.buttonLevel);
    TEST_ASSERT_EQUAL_UINT64(1ULL << Pins::Power::usbDetect, sleepFake.powerMask);
    TEST_ASSERT_EQUAL_UINT64(45000000, sleepFake.timerUs);
}

void test_inverted_buttons_keep_back_button_wake() {
    Pins::Buttons::buttonEnter = 46;
    enterSleep(0);
    TEST_ASSERT_EQUAL(Pins::Buttons::buttonBack, sleepFake.button);
    TEST_ASSERT_EQUAL(1, sleepFake.deepSleeps);
    Pins::Buttons::buttonEnter = 12;
}

void test_near_expiry_and_failed_wake_configuration_stay_awake() {
    enterSleep(1);
    TEST_ASSERT_EQUAL(0, sleepFake.deepSleeps);
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP, idleState);
    sleepFake.wakeError = true;
    enterSleep(0);
    TEST_ASSERT_EQUAL(0, sleepFake.deepSleeps);
    TEST_ASSERT_TRUE(sleepFake.panelCommands.empty());
}

void test_power_arriving_during_preparation_cancels_sleep() {
    enterSleep(0, []() { sleepFake.power = true; });
    TEST_ASSERT_EQUAL(0, sleepFake.deepSleeps);
    TEST_ASSERT_TRUE(sleepFake.panelCommands.empty());
}

void test_serial_rx_wakes_every_software_sleep_state() {
    initSerialWake();
    for (const auto state : {IdleState::IDLE, IdleState::PSEUDO_SLEEP, IdleState::SLEEP}) {
        idleState = state;
        _mock_millis += 100;
        Serial.handler(nullptr, nullptr, ARDUINO_HW_CDC_RX_EVENT, nullptr);
        TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleState);
        TEST_ASSERT_EQUAL_UINT32(millis(), lastInteraction);
    }
}

void test_serial_reset_wakes_but_log_transmission_does_not() {
    initSerialWake();
    idleState = IdleState::PSEUDO_SLEEP;
    Serial.handler(nullptr, nullptr, ARDUINO_HW_CDC_TX_EVENT, nullptr);
    TEST_ASSERT_EQUAL(IdleState::PSEUDO_SLEEP, idleState);
    Serial.handler(nullptr, nullptr, ARDUINO_HW_CDC_BUS_RESET_EVENT, nullptr);
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleState);
}

void test_late_serial_wake_restores_panel_and_peripherals() {
    initSerialWake();
    sleepFake.duringPanelSleep = []() {
        ++_mock_millis;
        Serial.handler(nullptr, nullptr, ARDUINO_HW_CDC_RX_EVENT, nullptr);
    };
    enterSleep(0);
    TEST_ASSERT_EQUAL(0, sleepFake.deepSleeps);
    TEST_ASSERT_EQUAL(0, sleepFake.resetLow);
    TEST_ASSERT_EQUAL(ST77XX_SLPOUT, sleepFake.panelCommands.back());
    TEST_ASSERT_FALSE(sleepFake.inaAsleep);
    TEST_ASSERT_EQUAL(255, sleepFake.brightness);
    TEST_ASSERT_EQUAL(IdleState::NOT_IDLE, idleState);
}

void runSleepTests() {
    RUN_TEST(test_power_blocks_sleep_before_disconnect_or_panel_shutdown);
    RUN_TEST(test_usb_host_blocks_sleep_even_without_power_sense);
    RUN_TEST(test_battery_sleep_preserves_button_timer_and_adds_power_wake);
    RUN_TEST(test_inverted_buttons_keep_back_button_wake);
    RUN_TEST(test_near_expiry_and_failed_wake_configuration_stay_awake);
    RUN_TEST(test_power_arriving_during_preparation_cancels_sleep);
    RUN_TEST(test_serial_rx_wakes_every_software_sleep_state);
    RUN_TEST(test_serial_reset_wakes_but_log_transmission_does_not);
    RUN_TEST(test_late_serial_wake_restores_panel_and_peripherals);
}
