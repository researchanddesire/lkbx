#include <Arduino.h>
#include <NimBLEDevice.h>
#include <unity.h>

#include "structs/Version.h"
#include "tasks/TaskParams.h"
#include "utils/getEfuseMac.h"

#include "test_utils.h"

static constexpr char SVC_UUID[] = "522b443a-4c4b-4258-0001-420badbabe69";
static constexpr char CHR_UUID[] = "522b443a-4c4b-4258-0002-420badbabe69";

// ── Tests ───────────────────────────────────────────────────────────────

void test_nimble_initialized() {
    TEST_ASSERT_TRUE_MESSAGE(NimBLEDevice::getInitialized(),
                             "NimBLE should be initialized after initBoard()");
}

void test_nimble_task_running() {
    TEST_ASSERT_NOT_NULL_MESSAGE(TaskHandles::nimble,
                                 "nimbleTask handle should not be null");

    eTaskState state = eTaskGetState(TaskHandles::nimble);
    char msg[128];
    snprintf(msg, sizeof(msg), "nimbleTask state: %d (0=Running 1=Ready 2=Blocked "
             "3=Suspended 4=Deleted)", state);
    TEST_MESSAGE(msg);

    TEST_ASSERT_TRUE_MESSAGE(
        state != eDeleted && state != eInvalid,
        "nimbleTask should not be deleted or invalid");
}

void test_ble_server_exists() {
    NimBLEServer* srv = NimBLEDevice::getServer();
    TEST_ASSERT_NOT_NULL_MESSAGE(srv, "NimBLE server should be created");

    char msg[64];
    snprintf(msg, sizeof(msg), "BLE server connected clients: %d",
             srv->getConnectedCount());
    TEST_MESSAGE(msg);
}

void test_ble_service_exists() {
    NimBLEServer* srv = NimBLEDevice::getServer();
    TEST_ASSERT_NOT_NULL_MESSAGE(srv, "Server must exist");

    NimBLEService* svc = srv->getServiceByUUID(SVC_UUID);
    TEST_ASSERT_NOT_NULL_MESSAGE(svc,
                                 "LKBX BLE service should be registered");
}

void test_ble_characteristic_exists() {
    NimBLEServer* srv = NimBLEDevice::getServer();
    TEST_ASSERT_NOT_NULL_MESSAGE(srv, "Server must exist");

    NimBLEService* svc = srv->getServiceByUUID(SVC_UUID);
    TEST_ASSERT_NOT_NULL_MESSAGE(svc, "Service must exist");

    NimBLECharacteristic* chr = svc->getCharacteristic(CHR_UUID);
    TEST_ASSERT_NOT_NULL_MESSAGE(chr,
                                 "LKBX characteristic should exist in service");
}

void test_ble_characteristic_properties() {
    NimBLEServer* srv = NimBLEDevice::getServer();
    NimBLEService* svc = srv->getServiceByUUID(SVC_UUID);
    NimBLECharacteristic* chr = svc->getCharacteristic(CHR_UUID);
    TEST_ASSERT_NOT_NULL_MESSAGE(chr, "Characteristic must exist");

    uint16_t props = chr->getProperties();

    char msg[128];
    snprintf(msg, sizeof(msg), "Characteristic properties: 0x%04X", props);
    TEST_MESSAGE(msg);

    TEST_ASSERT_TRUE_MESSAGE(props & NIMBLE_PROPERTY::READ,
                             "Characteristic should have READ property");
    TEST_ASSERT_TRUE_MESSAGE(props & NIMBLE_PROPERTY::WRITE,
                             "Characteristic should have WRITE property");
    TEST_ASSERT_TRUE_MESSAGE(props & NIMBLE_PROPERTY::NOTIFY,
                             "Characteristic should have NOTIFY property");
}

void test_ble_characteristic_read_format() {
    NimBLEServer* srv = NimBLEDevice::getServer();
    NimBLEService* svc = srv->getServiceByUUID(SVC_UUID);
    NimBLECharacteristic* chr = svc->getCharacteristic(CHR_UUID);
    TEST_ASSERT_NOT_NULL_MESSAGE(chr, "Characteristic must exist");

    // onRead callback sets the value fresh on each BLE client read.
    // Without a connected client the value may be empty or stale.
    String val = chr->getValue().c_str();

    char msg[256];
    snprintf(msg, sizeof(msg), "Characteristic value: \"%s\" (len=%d)",
             val.c_str(), val.length());
    TEST_MESSAGE(msg);

    // Format: "MAC;ChipModel;wifiConnected;MD5;version"
    if (val.length() > 0) {
        int semicolons = 0;
        for (unsigned int i = 0; i < val.length(); i++) {
            if (val.charAt(i) == ';') semicolons++;
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            4, semicolons,
            "Device info should have 4 semicolons (MAC;Chip;WiFi;MD5;Ver)");

        String mac = getMacAddresss();
        TEST_ASSERT_TRUE_MESSAGE(
            val.startsWith(mac),
            "Device info should start with MAC address");

        TEST_ASSERT_TRUE_MESSAGE(
            val.endsWith(currentVersionString),
            "Device info should end with firmware version");
    } else {
        TEST_MESSAGE("Value empty (expected — no BLE client has triggered onRead)");
    }
}

void test_ble_advertising_active() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    TEST_ASSERT_NOT_NULL_MESSAGE(adv, "Advertising object should exist");

    TEST_ASSERT_TRUE_MESSAGE(adv->isAdvertising(),
                             "BLE should be actively advertising");
}

// ── Registration ────────────────────────────────────────────────────────

void register_ble_tests() {
    RUN_TEST(test_nimble_initialized);
    RUN_TEST(test_nimble_task_running);
    RUN_TEST(test_ble_server_exists);
    RUN_TEST(test_ble_service_exists);
    RUN_TEST(test_ble_characteristic_exists);
    RUN_TEST(test_ble_characteristic_properties);
    RUN_TEST(test_ble_characteristic_read_format);
    RUN_TEST(test_ble_advertising_active);
}
