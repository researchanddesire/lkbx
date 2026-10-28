#include "hardwareHealth.h"

#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "FirmwareProvenance.h"
#include "getId.h"

#ifndef FIRMWARE_BUILD_SHA
#define FIRMWARE_BUILD_SHA "unknown"
#endif
#ifndef FIRMWARE_TRACK
#define FIRMWARE_TRACK "development"
#endif

namespace {
uint32_t bootId = 0;
std::atomic<uint32_t> menuProgressMs{0};
uint32_t lastHeartbeatMs = 0;

unsigned long long uptimeMs() {
    return static_cast<unsigned long long>(esp_timer_get_time() / 1000);
}
}  // namespace

void hardwareHealthBoot() {
    bootId = esp_random();
#if CONFIG_SPIRAM_MODE_QUAD
    constexpr const char* variant = "r2";
#elif CONFIG_SPIRAM_MODE_OCT
    constexpr const char* variant = "r8";
#else
    constexpr const char* variant = "unknown";
#endif
    const auto imageHash = firmware::provenance::runningImageSha256();
    // The IDF console routes to UART and the configured secondary USB console.
    // Arduino Serial alone only reaches native USB on these board profiles.
    esp_log_write(ESP_LOG_INFO, "HardwareHealth",
                  "LKBX_HEALTH {\"event\":\"boot\",\"boot_id\":%lu,"
                  "\"uptime_ms\":%llu,\"build_sha\":\"%s\","
                  "\"track\":\"%s\",\"variant\":\"%s\",\"device_id\":\"%s\","
                  "\"image_sha256\":\"%s\",\"reset_reason\":%d}\n",
                  static_cast<unsigned long>(bootId), uptimeMs(),
                  FIRMWARE_BUILD_SHA, FIRMWARE_TRACK, variant, getId().c_str(),
                  imageHash.c_str(), static_cast<int>(esp_reset_reason()));
    // Observe the driver event, including a delayed connection after preflight
    // initially continued offline. Register before initWM starts Wi-Fi.
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
        hardwareHealthWifiConnected();
    }, ARDUINO_EVENT_WIFI_STA_GOT_IP);
}

void hardwareHealthWifiConnected() {
    esp_log_write(ESP_LOG_INFO, "HardwareHealth",
                  "LKBX_HEALTH {\"event\":\"wifi_connected\",\"boot_id\":%lu,"
                  "\"uptime_ms\":%llu,\"ip\":\"%s\"}\n",
                  static_cast<unsigned long>(bootId), uptimeMs(),
                  WiFi.localIP().toString().c_str());
}

void hardwareHealthMqttConnected(const char* broker) {
    esp_log_write(ESP_LOG_INFO, "HardwareHealth",
                  "LKBX_HEALTH {\"event\":\"mqtt_connected\",\"boot_id\":%lu,"
                  "\"uptime_ms\":%llu,\"broker\":\"%s\"}\n",
                  static_cast<unsigned long>(bootId), uptimeMs(), broker);
}

void hardwareHealthMenuProgress() {
    menuProgressMs.store(millis(), std::memory_order_relaxed);
}

void hardwareHealthHeartbeat(bool atMainMenu) {
    const uint32_t now = millis();
    if (now - lastHeartbeatMs < 5000) return;
    lastHeartbeatMs = now;
    const uint32_t menuMs = menuProgressMs.load(std::memory_order_relaxed);
    esp_log_write(ESP_LOG_INFO, "HardwareHealth",
                  "LKBX_HEALTH {\"event\":\"heartbeat\",\"boot_id\":%lu,"
                  "\"uptime_ms\":%llu,\"main_menu\":%s,\"menu_age_ms\":%lu}\n",
                  static_cast<unsigned long>(bootId), uptimeMs(),
                  atMainMenu && menuMs != 0 ? "true" : "false",
                  static_cast<unsigned long>(now - menuMs));
}
