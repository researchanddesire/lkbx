#include <WiFi.h>

#include "test_utils.h"

void test_wifi_connect() {
    WiFi.begin();

    bool connected = waitFor(
        []() { return WiFiClass::status() == WL_CONNECTED; }, 15000, 250);

    TEST_ASSERT_TRUE_MESSAGE(connected,
                             "WiFi should connect within 15s. "
                             "Ensure credentials are saved on device.");

    bool hasIp = waitFor(
        []() {
            auto ip = WiFi.localIP();
            return ip != IPAddress(INADDR_NONE) &&
                   ip != IPAddress(INADDR_ANY);
        },
        10000, 250);
    TEST_ASSERT_TRUE_MESSAGE(hasIp, "Should get a valid IP (not 0.0.0.0)");

    testWifiUp = true;

    char msg[64];
    snprintf(msg, sizeof(msg), "WiFi: %s",
             WiFi.localIP().toString().c_str());
    TEST_MESSAGE(msg);
}

void register_wifi_tests() {
    RUN_TEST(test_wifi_connect);
}
