#include "rad_ble.h"

#include <WiFi.h>

#include "FirmwareProvenance.h"
#include "utils/breakTiming.h"
#include "utils/freezeState.h"

#include "constants/PerferenceKeys.h"
#include "constants/Pins.h"
#include "services/buttons.h"
#include "services/encoder.h"
#include "services/ina.h"
#include "services/lastInteraction.h"
#include "services/machine.h"
#include "services/magnetics.h"
#include "services/mqtt.h"
#include "services/serverOnline.h"
#include "services/settings.h"
#include "state/Events.h"
#include "state/Guard.h"
#include "state/LKBX.h"
#include "structs/Version.h"

#ifndef FIRMWARE_BUILD_SHA
#define FIRMWARE_BUILD_SHA "unknown"
#endif

namespace {

bool blePaused = false;

constexpr uint16_t R = radble::RESOURCE_READABLE |
                       radble::RESOURCE_AVAILABLE;
constexpr uint16_t RW = R | radble::RESOURCE_WRITABLE |
                        radble::RESOURCE_LEASE_REQUIRED;
constexpr uint16_t RWS = RW | radble::RESOURCE_SAFETY_CRITICAL;
constexpr uint16_t RS = R | radble::RESOURCE_STREAMABLE;
constexpr uint16_t RWT = RW | radble::RESOURCE_STREAMABLE;
constexpr uint16_t RP = R | radble::RESOURCE_PERSISTENT;
constexpr uint16_t RWP = RW | radble::RESOURCE_PERSISTENT;

const radble::Resource RESOURCES[] = {
    {"essential", "essential.live", "essential", "object", "", RS,
     "{\"characteristic\":\"2010\",\"changeDriven\":true,\"maxRateHz\":4}"},
    {"device_name", "device.name", "setting", "string", "", RWP,
     "{\"maxBytes\":24,\"emptyResets\":true}"},
    {"firmware_provenance", "device.firmwareProvenance", "setting", "object",
     "", R, ""},
    {"enter", "button.enter", "button", "bool", "", RS,
     "{\"events\":[\"click\",\"double\",\"long\"]}"},
    {"back", "button.back", "button", "bool", "", RS,
     "{\"events\":[\"click\",\"double\",\"long\"]}"},
    {"power", "button.power", "button", "bool", "", RS, ""},
    {"encoder", "encoder.main", "encoder", "int", "ticks", RWT,
     "{\"min\":0,\"max\":100}"},
    {"hall1", "magnetic.hall1", "magnetic", "bool", "", RS, ""},
    {"hall2", "magnetic.hall2", "magnetic", "bool", "", RS, ""},
    {"tmr1", "magnetic.tmr1", "magnetic", "bool", "", RS, ""},
    {"tmr2", "magnetic.tmr2", "magnetic", "bool", "", RS, ""},
    {"tmr3", "magnetic.tmr3", "magnetic", "bool", "", RS, ""},
    {"paddle", "magnetic.paddleLocked", "magnetic", "bool", "", RS, ""},
    {"paddle_raw", "magnetic.paddleLockedRaw", "magnetic", "bool", "", RS,
     ""},
    {"backplate", "magnetic.backplateClosed", "magnetic", "bool", "", RS,
     ""},
    {"ina_available", "power.inaAvailable", "power", "bool", "", R, ""},
    {"usb", "power.usb", "power", "bool", "", RS, ""},
    {"voltage", "power.voltage", "power", "float", "V", RS, ""},
    {"battery_percent", "power.batteryPercent", "power", "int", "percent", RS,
     "{\"min\":0,\"max\":100}"},
    {"current", "power.current", "power", "float", "mA", RS, ""},
    {"power_mw", "power.power", "power", "float", "mW", RS, ""},
    {"charging", "power.charging", "power", "bool", "", RS, ""},
    {"state_lock", "lockbox.stateLocked", "motion", "bool", "", RS, ""},
    {"calibrated", "lockbox.calibrated", "motion", "bool", "", RS, ""},
    {"motor_calibrated", "lockbox.motorCalibrated", "setting", "bool", "",
     RP, ""},
    {"motor_inverted", "lockbox.motorInverted", "setting", "bool", "", RP,
     ""},
    {"button_calibrated", "lockbox.buttonCalibrated", "setting", "bool", "",
     RP, ""},
    {"button_inverted", "lockbox.buttonInverted", "setting", "bool", "", RP,
     ""},
    {"emergency_blocked", "lockbox.emergencyBlocked", "setting", "bool", "",
     RP, ""},
    {"emergency_end", "lockbox.emergencyEnd", "setting", "int", "ms", RP,
     ""},
    {"lock_end", "session.lockEnd", "setting", "int", "ms", RP, ""},
    {"break_active", "session.breakActive", "setting", "bool", "", RP, ""},
    {"session_id", "session.id", "setting", "string", "", RP, ""},
    {"lock_start", "session.lockStart", "setting", "int", "ms", RP, ""},
    {"break_enabled", "session.breakEnabled", "setting", "bool", "", RP,
     ""},
    {"break_max", "session.breakMax", "setting", "int", "ms", RP, ""},
    {"break_reg", "session.breakRegularity", "setting", "int", "ms", RP,
     ""},
    {"break_last", "session.breakLast", "setting", "int", "ms", RP, ""},
    {"time_display", "session.timeDisplay", "setting", "bool", "", RP, ""},
    {"elapsed_display", "session.displayElapsedTime", "setting", "bool", "",
     RP, ""},
    {"frozen", "session.frozen", "setting", "bool", "", RP, ""},
    {"frozen_at", "session.frozenAt", "setting", "int", "ms", RP, ""},
    {"freeze_reason", "session.freezeReason", "setting", "string", "", RP,
     ""},
    {"session_apply", "session.definition", "setting", "object", "", RWP,
     "{\"operation\":\"session.apply\"}"},
    {"alpha", "device.alphaChannel", "setting", "bool", "", RWP, ""},
    {"onboarded", "device.onboarded", "setting", "bool", "", RP, ""},
    {"paired", "device.paired", "setting", "bool", "", RP, ""},
    {"last_updated", "device.lastUpdated", "setting", "int", "ms", RP, ""},
    {"idle", "device.idleState", "setting", "int", "enum", R, ""},
    {"clock", "connectivity.clock", "connectivity", "object", "", RS, ""},
    {"mqtt", "connectivity.mqtt", "connectivity", "bool", "", RS, ""},
    {"server", "connectivity.server", "connectivity", "bool", "", RS, ""},
    {"lock", "target.lock", "target", "event", "", RWS, ""},
    {"unlock", "target.unlock", "target", "event", "", RWS, ""},
    {"lock_timer", "target.lockTimer", "target", "event", "ms", RWS,
     "{\"args\":[\"lockEnd\"],\"min\":1000}"},
    {"break", "target.break", "target", "event", "", RWS, ""},
    {"break_end", "target.breakEnd", "target", "event", "", RWS, ""},
    {"home", "target.home", "target", "event", "", RW, ""},
    {"settings", "target.settings", "target", "event", "", RW, ""},
    {"sleep", "target.sleep", "target", "event", "", RW, ""},
    {"pairing", "target.pairing", "target", "event", "", RW, ""},
    {"wifi_setup", "target.wifi", "target", "event", "", RW, ""},
    {"offered_accept", "target.offeredAccept", "target", "event", "", RWS,
     ""},
    {"offered_decline", "target.offeredDecline", "target", "event", "", RW,
     ""},
    {"emergency_unlock", "target.emergencyUnlock", "target", "event", "",
     RWS, ""},
    {"recalibrate", "target.recalibrate", "target", "event", "", RWS, ""},
    {"restart", "target.restart", "target", "event", "", RW, ""},
    {"clear_session", "target.clearSession", "target", "event", "", RWS,
     ""},
    {"update", "target.update", "target", "event", "", RW, ""},
    {"sync_session", "event.syncSession", "event", "event", "", RW, ""},
    {"done", "event.done", "event", "event", "", RW, ""},
    {"error", "event.error", "event", "event", "", RW,
     "{\"args\":[\"error\"],\"min\":0,\"max\":16}"},
    {"wifi_connected", "event.wifiConnected", "event", "event", "", RW,
     ""},
    {"hue", "display.hueShift", "setting", "int", "degrees", RWP,
     "{\"min\":0,\"max\":255}"},
    {"backlight", "indicator.backlight", "indicator", "int", "percent", RW,
     "{\"min\":0,\"max\":100}"},
    {"wifi", "connectivity.wifi", "connectivity", "object", "", RS, ""},
};

void requestNetworkOtaTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(500));
    if (lkbx != nullptr && lkbx->sm != nullptr)
        lkbx->sm->process_event(ForceUpdate{});
    vTaskDelete(nullptr);
}

String stateName();

bool selectMenuItem(MenuItemE item) {
    if (lkbx == nullptr || lkbx->sm == nullptr || lkbx->activeMenu == nullptr)
        return false;
    for (int index = 0; index < lkbx->menuSize; ++index) {
        if (lkbx->activeMenu[index].id != item) continue;
        rotaryEncoder.setEncoderValue(index);
        lkbx->sm->process_event(EncoderValue{.value = index});
        return lkbx->sm->process_event(ButtonPress{});
    }
    return false;
}

bool reachHome() {
    String state = stateName();
    if (state == "home.idle") return true;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (!lkbx->sm->process_event(BackPress{})) return false;
        state = stateName();
        if (state == "home.idle") return true;
    }
    return false;
}

bool reachSettings() {
    const String state = stateName();
    if (state == "home.settings") return true;
    return reachHome() && selectMenuItem(MenuItemE::SETTINGS) &&
           stateName() == "home.settings";
}

bool reachSettingsItem(MenuItemE item) {
    return reachSettings() && selectMenuItem(item);
}

void requestRestartTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(500));
    reachSettingsItem(MenuItemE::RESTART);
    vTaskDelete(nullptr);
}

String stateName() {
    String state = "starting";
    if (lkbx != nullptr && lkbx->sm != nullptr)
        lkbx->sm->visit_current_states(
            [&state](auto current) { state = current.c_str(); });
    return state;
}

struct PowerTelemetry {
    bool valid = false;
    uint32_t updatedAt = 0;
    float voltage = 0.0f;
    float currentMa = 0.0f;
    float powerMw = 0.0f;
    int percent = 0;
};

PowerTelemetry powerTelemetry;

void refreshPowerTelemetry() {
    const uint32_t now = millis();
    if (powerTelemetry.valid && now - powerTelemetry.updatedAt < 500U) return;
    powerTelemetry.voltage = inaReady ? ina.getBusVoltage_V() : 0.0f;
    powerTelemetry.currentMa = inaReady ? ina.getCurrent_mA() : 0.0f;
    powerTelemetry.powerMw = inaReady ? ina.getPower_mW() : 0.0f;
    powerTelemetry.percent = constrain(
        static_cast<int>((powerTelemetry.voltage - 3.0f) * 83.333f), 0, 100);
    powerTelemetry.updatedAt = now;
    powerTelemetry.valid = true;
}

radble::Result storedValue(const String& path) {
    if (xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(250)) != pdTRUE)
        return radble::Result::failure("busy", "Settings storage is busy");
    JsonDocument document;
    document["path"] = path;
    if (path == "lockbox.motorCalibrated" ||
        path == "lockbox.motorInverted" ||
        path == "lockbox.buttonCalibrated" ||
        path == "lockbox.buttonInverted" ||
        path == "lockbox.emergencyBlocked" || path == "lockbox.emergencyEnd" ||
        path == "device.alphaChannel" || path == "device.onboarded" ||
        path == "device.paired" || path == "device.lastUpdated" ||
        path == "device.idleState") {
        preferences.begin(Keys::base);
        if (path == "lockbox.motorCalibrated")
            document["value"] =
                preferences.getBool(Keys::Base::isMotorCalibrated, false);
        else if (path == "lockbox.motorInverted")
            document["value"] =
                preferences.getBool(Keys::Base::isMotorInverted, false);
        else if (path == "lockbox.buttonCalibrated")
            document["value"] =
                preferences.getBool(Keys::Base::isButtonCalibrated, false);
        else if (path == "lockbox.buttonInverted")
            document["value"] =
                preferences.getBool(Keys::Base::isButtonInverted, false);
        else if (path == "lockbox.emergencyBlocked")
            document["value"] =
                preferences.getBool(Keys::Base::isEBlocked, false);
        else if (path == "lockbox.emergencyEnd")
            document["value"] =
                preferences.getULong64(Keys::Base::emergencyEnd, 0);
        else if (path == "device.alphaChannel")
            document["value"] = preferences.getBool(Keys::Base::isAlpha, false);
        else if (path == "device.onboarded")
            document["value"] =
                preferences.getBool(Keys::Base::hasBeenOnboarded, false);
        else if (path == "device.lastUpdated")
            document["value"] =
                preferences.getULong64(Keys::Base::lastUpdated, 0);
        else if (path == "device.idleState")
            document["value"] = static_cast<int>(idleState);
        else
            document["value"] = preferences.getBool(Keys::Base::isPaired, false);
        preferences.end();
    } else if (path.startsWith("session.")) {
        preferences.begin(Keys::session);
        if (path == "session.lockEnd")
            document["value"] = preferences.getULong64(Keys::Session::lockEnd, 0);
        else if (path == "session.breakActive")
            document["value"] =
                preferences.getBool(Keys::Session::isActiveBreak, false);
        else if (path == "session.id")
            document["value"] = preferences.getString(Keys::Session::sessionId, "");
        else if (path == "session.lockStart")
            document["value"] = preferences.getULong64(Keys::Session::lockStart, 0);
        else if (path == "session.breakEnabled")
            document["value"] = preferences.getBool(Keys::Session::isBreak, false);
        else if (path == "session.breakMax")
            document["value"] = preferences.getULong64(Keys::Session::breakMax, 0);
        else if (path == "session.breakRegularity")
            document["value"] = preferences.getULong64(Keys::Session::breakReg, 0);
        else if (path == "session.breakLast")
            document["value"] = preferences.getULong64(Keys::Session::breakLast, 0);
        else if (path == "session.timeDisplay")
            document["value"] = preferences.getBool(Keys::Session::isTimeDisp, true);
        else if (path == "session.displayElapsedTime")
            document["value"] = preferences.getBool(
                Keys::Session::displayElapsedTime, false);
        else if (path == "session.frozen")
            document["value"] =
                preferences.getBool(Keys::Session::isFrozen, false);
        else if (path == "session.frozenAt")
            document["value"] =
                preferences.getULong64(Keys::Session::frozenAt, 0);
        else if (path == "session.freezeReason") {
            // The admin frame is size-limited: bound the reason on the wire even
            // though the persisted copy may be longer.
            char reason[kFreezeReasonWireBytes];
            boundedFreezeReason(
                preferences.getString(Keys::Session::freezeReason, "")
                    .c_str(),
                reason, sizeof(reason), kFreezeReasonWireChars);
            document["value"] = reason;
        }
        else if (path == "session.definition") {
            document["sessionId"] =
                preferences.getString(Keys::Session::sessionId, "");
            document["lockState"] =
                preferences.getString(Keys::Session::lockState, "");
            document["lockStart"] =
                preferences.getULong64(Keys::Session::lockStart, 0);
            document["lockEnd"] =
                preferences.getULong64(Keys::Session::lockEnd, 0);
            document["breakEnabled"] =
                preferences.getBool(Keys::Session::isBreak, false);
            document["breakKeyholderOnly"] =
                preferences.getBool(Keys::Session::breakKeyholderOnly, false);
            document["breakMax"] =
                preferences.getULong64(Keys::Session::breakMax, 0);
            document["breakRegularity"] =
                preferences.getULong64(Keys::Session::breakReg, 0);
            document["breakLast"] =
                preferences.getULong64(Keys::Session::breakLast, 0);
            document["emergencyUnlock"] =
                preferences.getBool(Keys::Session::isEUnlock, false);
            document["timeDisplay"] =
                preferences.getBool(Keys::Session::isTimeDisp, true);
            document["displayElapsedTime"] = preferences.getBool(
                Keys::Session::displayElapsedTime, false);
            // Chaster freeze state (observed by the validation harness)
            document["isFrozen"] =
                preferences.getBool(Keys::Session::isFrozen, false);
            document["frozenAt"] =
                preferences.getULong64(Keys::Session::frozenAt, 0);
            const String freezeReason =
                preferences.getString(Keys::Session::freezeReason, "");
            char boundedReason[kFreezeReasonWireBytes];
            boundedFreezeReason(freezeReason.c_str(), boundedReason,
                                sizeof(boundedReason), kFreezeReasonWireChars);
            if (boundedReason[0] == '\0')
                document["freezeReason"] = nullptr;
            else
                document["freezeReason"] = boundedReason;
        } else {
            preferences.end();
            xSemaphoreGive(preferencesMutex);
            return radble::Result::failure("unknown_path", "Unknown session setting");
        }
        preferences.end();
    } else {
        xSemaphoreGive(preferencesMutex);
        return radble::Result::failure("unknown_path", "Unknown setting path");
    }
    xSemaphoreGive(preferencesMutex);
    String output;
    serializeJson(document, output);
    return radble::Result::success(output);
}

radble::Result handleCommand(JsonObjectConst request, void*) {
    if (lkbx == nullptr || lkbx->sm == nullptr)
        return radble::Result::failure("not_ready", "LKBX is still starting");
    const String operation = request["op"] | "";
    const String path = request["path"] | "";
    const JsonObjectConst args = request["args"].as<JsonObjectConst>();

    if (operation == "ota.start" && String(args["transport"] | "") == "wifi") {
        if (WiFi.status() != WL_CONNECTED)
            return radble::Result::failure("network_failed", "Wi-Fi is disconnected");
        const String state = stateName();
        if (state.startsWith("locking") || state.startsWith("unlocking") ||
            state.startsWith("emergency.unlock") ||
            state.startsWith("update"))
            return radble::Result::failure(
                "invalid_state", "LKBX motor or updater is active");
        if (xTaskCreate(requestNetworkOtaTask, "rad-net-ota", 2048, nullptr, 1,
                        nullptr) != pdPASS)
            return radble::Result::failure("busy", "Could not schedule network OTA");
        return radble::Result::success(R"({"transport":"wifi","requested":true})");
    }

    if (operation == "sensor.read" || operation == "setting.read") {
        if (path == "device.firmwareProvenance") {
            const auto snapshot = firmware::provenance::runningSnapshot(
                FIRMWARE_TRACK, "lkbx", VERSION, FIRMWARE_BUILD_SHA);
            JsonDocument document;
            document["path"] = path;
            document["origin"] = snapshot.origin;
            document["keyId"] = snapshot.keyId;
            document["provenanceId"] = snapshot.provenanceId;
            document["imageSha256"] = snapshot.imageSha256;
            document["compactJws"] = snapshot.token;
            String output;
            serializeJson(document, output);
            return radble::Result::success(output);
        }
        if (path.startsWith("lockbox.motor") ||
            path.startsWith("lockbox.button") ||
            path.startsWith("lockbox.emergency") ||
            path.startsWith("session.") || path.startsWith("device."))
            return storedValue(path);
        JsonDocument document;
        document["path"] = path;
        if (path == "lockbox.stateLocked")
            document["value"] = Motor::isStateLocked();
        else if (path == "lockbox.calibrated")
            document["value"] = lkbx->motor.isCalibrated;
        else if (path == "button.enter")
            document["value"] =
                digitalRead(Pins::Buttons::buttonEnter) == LOW;
        else if (path == "button.back")
            document["value"] =
                digitalRead(Pins::Buttons::buttonBack) == LOW;
        else if (path == "button.power")
            document["value"] =
                digitalRead(Pins::Buttons::buttonPowerDet) == LOW;
        else if (path == "encoder.main")
            document["value"] = rotaryEncoder.readEncoder();
        else if (path == "magnetic.hall1")
            document["value"] = digitalRead(Pins::Magnetics::hall1) == LOW;
        else if (path == "magnetic.hall2")
            document["value"] = digitalRead(Pins::Magnetics::hall2) == LOW;
        else if (path == "magnetic.tmr1")
            document["value"] = digitalRead(Pins::Magnetics::tmr1) == LOW;
        else if (path == "magnetic.tmr2")
            document["value"] = digitalRead(Pins::Magnetics::tmr2) == LOW;
        else if (path == "magnetic.tmr3")
            document["value"] = digitalRead(Pins::Magnetics::tmr3) == LOW;
        else if (path == "magnetic.paddleLocked")
            document["value"] = getStablePaddleLocked();
        else if (path == "magnetic.paddleLockedRaw")
            document["value"] = isPaddleLocked();
        else if (path == "magnetic.backplateClosed")
            document["value"] = isBackplateClosed();
        else if (path == "power.inaAvailable")
            document["value"] = inaReady;
        else if (path == "power.usb")
            document["value"] = digitalRead(Pins::Power::usbDetect) == HIGH;
        else if (path == "power.charging")
            document["value"] = digitalRead(Pins::Power::usbDetect) == HIGH;
        else if (path == "power.voltage" ||
                 path == "power.batteryPercent" ||
                 path == "power.current" || path == "power.power") {
            refreshPowerTelemetry();
            if (path == "power.voltage")
                document["value"] = powerTelemetry.voltage;
            else if (path == "power.batteryPercent")
                document["value"] = powerTelemetry.percent;
            else if (path == "power.current")
                document["value"] = powerTelemetry.currentMa;
            else
                document["value"] = powerTelemetry.powerMw;
        } else if (path == "connectivity.clock") {
            document["synced"] = isTimeSynced;
            document["wallMs"] = currentWallClockMs();
            document["monotonicMs"] = currentMonotonicMs();
        } else if (path == "connectivity.mqtt")
            document["value"] = mqttConnected;
        else if (path == "connectivity.server")
            document["value"] = isServerOnline(WiFi.status() == WL_CONNECTED);
        else if (path == "connectivity.wifi") {
            const bool connected = WiFi.status() == WL_CONNECTED;
            document["connected"] = connected;
            if (connected) {
                document["rssi"] = WiFi.RSSI();
                document["ip"] = WiFi.localIP().toString();
            }
        }
        else if (path == "display.hueShift") {
            if (xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(250)) != pdTRUE)
                return radble::Result::failure("busy",
                                               "Settings storage is busy");
            preferences.begin(Keys::base);
            const int value = preferences.getInt(Keys::Base::hueShift, 0);
            preferences.end();
            xSemaphoreGive(preferencesMutex);
            document["value"] = value;
        } else
            return radble::Result::failure("unknown_path", "Unknown resource path");
        String output;
        serializeJson(document, output);
        return radble::Result::success(output);
    }

    if (operation == "input.emit" || operation == "event.emit") {
        const String event = args["event"] | "click";
        bool handled = false;
        if (path == "button.enter") {
            if (event == "long")
                handled = lkbx->sm->process_event(LongPress{});
            else if (event == "click" || event == "double")
                handled = lkbx->sm->process_event(ButtonPress{});
            else
                return radble::Result::failure("invalid_value",
                                               "Unknown button event");
        } else if (path == "button.back") {
            if (event == "long")
                handled = lkbx->sm->process_event(BackLongPress{});
            else if (event == "click" || event == "double")
                handled = lkbx->sm->process_event(BackPress{});
            else
                return radble::Result::failure("invalid_value",
                                               "Unknown button event");
        } else if (operation == "event.emit" && path == "event.syncSession")
            handled = lkbx->sm->process_event(SyncSession{});
        else if (operation == "event.emit" && path == "event.done")
            handled = lkbx->sm->process_event(Done{});
        else if (operation == "event.emit" && path == "event.wifiConnected")
            handled = lkbx->sm->process_event(WifiConnected{});
        else if (operation == "event.emit" && path == "event.error") {
            if (!args["error"].is<int>() || args["error"].as<int>() < 0 ||
                args["error"].as<int>() >
                    static_cast<int>(ErrorT::OTA_FAILED))
                return radble::Result::failure("invalid_value", "Error code is out of range");
            handled = lkbx->sm->process_event(
                Error{.error = static_cast<ErrorT>(args["error"].as<int>())});
        }
        else
            return radble::Result::failure("unknown_path", "Unknown input path");
        if (!handled)
            return radble::Result::failure(
                "guard_rejected", "The current state rejected this event");
        Serial.printf("[RAD BLE][LKBX] input %s %s\n", path.c_str(), event.c_str());
        return radble::Result::success();
    }

    if (operation == "encoder.set" || operation == "encoder.delta") {
        if (path != "encoder.main")
            return radble::Result::failure("unknown_path", "Unknown encoder path");
        if ((operation == "encoder.delta" && !args["delta"].is<int>()) ||
            (operation == "encoder.set" && !args["value"].is<int>()))
            return radble::Result::failure("invalid_value",
                                           "An integer encoder value is required");
        const int value = operation == "encoder.delta"
                              ? rotaryEncoder.readEncoder() +
                                    (args["delta"] | 0)
                              : (args["value"] | 0);
        if (value < 0 || value > 100)
            return radble::Result::failure(
                "invalid_value", "Encoder value must be 0..100");
        rotaryEncoder.setEncoderValue(value);
        lkbx->sm->process_event(EncoderValue{.value = value});
        Serial.printf("[RAD BLE][LKBX] encoder=%d\n", value);
        return radble::Result::success("{\"value\":" + String(value) + "}");
    }

    if (operation == "target.set") {
        bool handled = false;
        if (path == "target.lock")
            handled = lkbx->sm->process_event(Lock{});
        else if (path == "target.unlock")
            handled = lkbx->sm->process_event(Unlock{});
        else if (path == "target.lockTimer") {
            if (!args["lockEnd"].is<uint64_t>())
                return radble::Result::failure(
                    "invalid_value", "lockEnd must be a positive uint64");
            const uint64_t lockEnd = args["lockEnd"].as<uint64_t>();
            if (lockEnd == 0)
                return radble::Result::failure("invalid_value", "lockEnd is required");
            handled = lkbx->sm->process_event(LockTimer{.lockEnd = lockEnd});
        } else if (path == "target.break") {
            const String state = stateName();
            if (state == "locked" || state == "locked.timer")
                handled = lkbx->sm->process_event(ButtonPress{});
            else if (state == "lockMenu")
                handled = true;
            if (handled && stateName() == "lockMenu")
                handled = selectMenuItem(MenuItemE::BREAK);
            if (handled && stateName() == "locked.breaking")
                handled = lkbx->sm->process_event(ButtonPress{});
        } else if (path == "target.breakEnd") {
            handled = lkbx->sm->process_event(Done{});
        } else if (path == "target.home") {
            handled = reachHome();
        } else if (path == "target.settings") {
            handled = reachSettings();
        } else if (path == "target.sleep") {
            const String state = stateName();
            handled = state == "lockMenu"
                          ? selectMenuItem(MenuItemE::SLEEP)
                          : reachHome() && selectMenuItem(MenuItemE::SLEEP);
        } else if (path == "target.pairing") {
            handled = reachSettingsItem(MenuItemE::PAIRING);
        } else if (path == "target.wifi") {
            const String state = stateName();
            handled = state == "lockMenu"
                          ? selectMenuItem(MenuItemE::WIFI_SETTINGS)
                          : reachSettingsItem(MenuItemE::WIFI_SETTINGS);
        } else if (path == "target.offeredAccept") {
            handled = stateName() == "acceptLock" &&
                      lkbx->sm->process_event(ButtonPress{});
        } else if (path == "target.offeredDecline") {
            handled = stateName() == "acceptLock" &&
                      lkbx->sm->process_event(BackPress{});
        } else if (path == "target.emergencyUnlock") {
            const String state = stateName();
            if (state == "locked" || state == "locked.timer")
                handled = lkbx->sm->process_event(ButtonPress{});
            else if (state == "lockMenu")
                handled = true;
            if (handled && stateName() == "lockMenu")
                handled = selectMenuItem(MenuItemE::EMERGENCY);
        } else if (path == "target.recalibrate") {
            handled = lkbx->sm->process_event(Recalibrate{});
        } else if (path == "target.restart") {
            if (stateName() == "home.idle" || stateName() == "home.settings")
                handled = xTaskCreate(requestRestartTask, "rad-restart", 2048,
                                      nullptr, 1, nullptr) == pdPASS;
        } else if (path == "target.clearSession") {
            const String state = stateName();
            handled = state == "home.idle" ||
                      lkbx->sm->process_event(Unlock{});
            if (handled && clearState() == SyncResult::FAILED)
                return radble::Result::failure(
                    "storage_failed", "Could not clear the lock session");
        } else if (path == "target.update") {
            const String state = stateName();
            handled = (state == "home.idle" || state == "home.settings" ||
                       state == "pairing" || state == "update.none") &&
                      lkbx->sm->process_event(ForceUpdate{});
        } else return radble::Result::failure("unknown_path", "Unknown target path");
        if (!handled)
            return radble::Result::failure(
                "guard_rejected", "The current state rejected this target");
        Serial.printf("[RAD BLE][LKBX] target %s\n", path.c_str());
        return radble::Result::success();
    }

    if (operation == "setting.write" && path == "display.hueShift") {
        if (!args["value"].is<int>())
            return radble::Result::failure("invalid_value",
                                           "An integer hue is required");
        const int value = args["value"].as<int>();
        if (value < 0 || value > 255)
            return radble::Result::failure("invalid_value",
                                           "Hue must be 0..255");
        if (xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(250)) != pdTRUE)
            return radble::Result::failure("busy", "Settings storage is busy");
        preferences.begin(Keys::base, false);
        const bool stored = preferences.putInt(Keys::Base::hueShift, value) > 0;
        preferences.end();
        xSemaphoreGive(preferencesMutex);
        if (!stored)
            return radble::Result::failure("storage_failed",
                                           "Could not store display hue");
        Serial.printf("[RAD BLE][LKBX] hueShift=%d\n", value);
        return radble::Result::success("{\"value\":" + String(value) + "}");
    }

    if (operation == "setting.write" && path == "device.alphaChannel") {
        if (!args["value"].is<bool>())
            return radble::Result::failure("invalid_value",
                                           "A boolean update channel is required");
        const bool value = args["value"] | false;
        if (xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(250)) != pdTRUE)
            return radble::Result::failure("busy", "Settings storage is busy");
        preferences.begin(Keys::base, false);
        const bool stored =
            preferences.putBool(Keys::Base::isAlpha, value) > 0;
        preferences.end();
        xSemaphoreGive(preferencesMutex);
        if (!stored)
            return radble::Result::failure("storage_failed",
                                           "Could not store update channel");
        return radble::Result::success(value ? "{\"value\":true}"
                                                : "{\"value\":false}");
    }

    if (operation == "session.apply" && path == "session.definition") {
        const JsonObjectConst sessionArgs =
            args["session"].as<JsonObjectConst>();
        if (sessionArgs.isNull() ||
            !sessionArgs["lockState"].is<const char *>() ||
            !sessionArgs["sessionId"].is<const char *>())
            return radble::Result::failure(
                "invalid_value", "A complete lock session is required");
        const String currentState = stateName();
        if (currentState.startsWith("locking") ||
            currentState.startsWith("emergency") ||
            currentState.startsWith("calibrate") ||
            currentState.startsWith("update"))
            return radble::Result::failure(
                "invalid_state", "Wait for the current lockbox operation to finish");
        const String lockState = sessionArgs["lockState"].as<String>();
        const String sessionId = sessionArgs["sessionId"].as<String>();
        if (sessionId.length() > 80 ||
            (lockState != Str::Protocol::LOCKED &&
             lockState != Str::Protocol::BREAK &&
             lockState != Str::Protocol::UNLOCKED))
            return radble::Result::failure(
                "invalid_value", "Session identity or lock state is invalid");
        const uint64_t lockEnd = sessionArgs["lockEnd"] | 0ULL;
        const uint64_t lockStart = sessionArgs["lockStart"] | 0ULL;
        const uint64_t breakMax = sessionArgs["breakMax"] | 0ULL;
        const uint64_t breakReg = sessionArgs["breakRegularity"] | 0ULL;
        const uint64_t breakLast = sessionArgs["breakLast"] | 0ULL;
        const bool timed = lockState != Str::Protocol::UNLOCKED && lockEnd != 0;
        const bool canApplyUnlocked =
            currentState == "home.idle" || currentState == "home.settings" ||
            currentState == "locked.ready" || currentState == "locked.ready.addTime" ||
            currentState == "locked" || currentState == "locked.timer" ||
            currentState == "locked.frozen" ||
            currentState == "lockMenu" || currentState == "lockMenu.sleep" ||
            currentState == "locked.break";
        const bool canApplyBreak =
            currentState == "locked" || currentState == "locked.timer" ||
            currentState == "locked.frozen" ||
            currentState == "lockMenu" || currentState == "lockMenu.sleep";
        const bool canApplyLock =
            currentState == "home.idle" || currentState == "home.settings" ||
            currentState == "locked.ready" || currentState == "locked.ready.addTime" ||
            currentState == "timer" || currentState == "locked" ||
            currentState == "locked.timer" || currentState == "locked.frozen" ||
            currentState == "lockMenu" ||
            currentState == "lockMenu.sleep";
        if ((lockState == Str::Protocol::UNLOCKED && !canApplyUnlocked) ||
            (lockState == Str::Protocol::BREAK && !canApplyBreak) ||
            (lockState == Str::Protocol::LOCKED && !canApplyLock))
            return radble::Result::failure(
                "invalid_state", "The current page cannot apply this session");
        if (timed && isTimeSynced &&
            lockEnd <= static_cast<uint64_t>(currentWallClockMs()))
            return radble::Result::failure(
                "invalid_value", "Timed lock end must be in the future");

        SyncResult saved = SyncResult::FAILED;
        SyncOutcome outcome;
        if (lockState == Str::Protocol::UNLOCKED) {
            saved = clearState();
        } else {
            saved = saveState({
                .isBreak = sessionArgs["breakEnabled"] | false,
                .lockEnd = lockEnd,
                .breakMax = breakMax,
                .breakReg = breakReg,
                .breakLast = breakLast,
                .duration = sessionArgs["duration"] | 0ULL,
                .isEUnlock = sessionArgs["emergencyUnlock"] | false,
                .lockStart = lockStart,
                .lockState = lockState,
                .sessionId = sessionId,
                .lockName = sessionArgs[Str::Json::LOCK_NAME] | "",
                .requestedBy = sessionArgs[Str::Json::REQUESTED_BY] | "",
                .isTestLock = sessionArgs["testLock"] | false,
                .isTimeDisp = sessionArgs["timeDisplay"] | true,
                .displayElapsedTime =
                    sessionArgs["displayElapsedTime"] | false,
                .breakKeyholderOnly =
                    sessionArgs["breakKeyholderOnly"] | false,
            }, &outcome);
            if (!timed && saved == SyncResult::LOCKED &&
                xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(250)) == pdTRUE) {
                preferences.begin(Keys::base, false);
                preferences.putBool(Keys::Base::isLockTimer, false);
                preferences.end();
                xSemaphoreGive(preferencesMutex);
            }
        }
        bool handled = false;
        if (saved == SyncResult::UNLOCKED)
            handled = currentState == "home.idle" ||
                      lkbx->sm->process_event(Unlock{});
        else if (saved == SyncResult::BREAK)
            handled = lkbx->sm->process_event(Break{});
        else if (saved != SyncResult::FAILED &&
                 (outcome.effect == SyncEffect::TIMER_META ||
                  outcome.effect == SyncEffect::NONE)) {
            if (outcome.freezeChanged) bumpFreezeDisplayVersion();
            // BLE may also change timed/permanent mode independently of the
            // session fields. Reconcile without re-entering the same screen.
            lkbx->sm->process_event(FreezeChange{});
            handled = true;
        }
        else if (saved == SyncResult::LOCKED && timed)
            handled = lkbx->sm->process_event(LockTimer{.lockEnd = lockEnd});
        else if (saved == SyncResult::LOCKED)
            handled = currentState == "locked" ||
                      lkbx->sm->process_event(Lock{});
        if (saved == SyncResult::FAILED)
            return radble::Result::failure("storage_failed", "Could not store the lock session");
        if (!handled)
            return radble::Result::failure(
                "guard_rejected", "The lockbox rejected the stored session");
        return radble::Result::success(
            "{\"lockState\":\"" + lockState + "\",\"lockEnd\":" +
            String(lockEnd) + ",\"eventAccepted\":true}");
    }

    if (operation == "indicator.set" && path == "indicator.backlight") {
        if (!args["value"].is<int>())
            return radble::Result::failure("invalid_value",
                                           "An integer brightness is required");
        const int value = args["value"].as<int>();
        if (value < 0 || value > 100)
            return radble::Result::failure("invalid_value",
                                           "Brightness must be 0..100");
        ledcWrite(0, map(value, 0, 100, 0, 255));
        Serial.printf("[RAD BLE][LKBX] backlight=%d\n", value);
        return radble::Result::success("{\"value\":" + String(value) + "}");
    }

    return radble::Result::failure("unsupported", "Operation is not supported");
}

String snapshot(radble::Surface surface, void*) {
    JsonDocument document;
    switch (surface) {
        case radble::Surface::State:
            document["state"] = stateName();
            document["locked"] = Motor::isStateLocked();
            document["paddleLocked"] = getStablePaddleLocked();
            document["calibrated"] = lkbx != nullptr && lkbx->motor.isCalibrated;
            document["idleState"] = static_cast<int>(idleState);
            document["mqtt"] = mqttConnected;
            break;
        case radble::Surface::Essential: {
            refreshPowerTelemetry();
            const bool locked = Motor::isStateLocked();
            document["state"] = stateName();
            document["powered"] = true;
            document["batteryPercent"] = powerTelemetry.percent;
            document["charging"] =
                digitalRead(Pins::Power::usbDetect) == HIGH;
            document["locked"] = locked;
            document["paddleLocked"] = getStablePaddleLocked();

            SessionTiming timing;
            bool timingAvailable = false;
            if (xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                timingAvailable = preferences.getSessionTiming(timing);
                xSemaphoreGive(preferencesMutex);
            }
            const bool frozen = timing.frozen;
            const uint64_t frozenAt = timing.frozenAt;
            const String& freezeReason = timing.freezeReason;
            const bool breakActive = timing.breakActive;
            const uint64_t lockEnd = timing.lockEnd;
            const uint64_t breakStart = timing.breakStart;
            const uint64_t breakMax = timing.breakMax;
            document["breakActive"] = breakActive;
            document["isFrozen"] = frozen;
            document["frozenAt"] = frozen ? frozenAt : 0;
            char boundedReason[kFreezeReasonWireBytes];
            boundedFreezeReason(freezeReason.c_str(), boundedReason,
                                sizeof(boundedReason), kFreezeReasonWireChars);
            if (boundedReason[0] == '\0')
                document["freezeReason"] = nullptr;
            else
                document["freezeReason"] = boundedReason;
            const int64_t wallMs = currentWallClockMs();
            if (timingAvailable && locked && isTimeSynced && wallMs > 0 &&
                lockEnd > 0) {
                if (frozen) {
                    // A frozen countdown is paused: report the time anchored at
                    // frozenAt instead of a live countdown. An anchor at or past
                    // the deadline is a valid expired anchor and reports 0; only
                    // an unknown anchor (null) leaves the remaining time unknown.
                    if (freezeAnchorKnown(frozenAt))
                        document["lockTimeRemainingMs"] =
                            frozenRemainingAtAnchor(frozenAt, lockEnd);
                    else
                        document["lockTimeRemainingMs"] = nullptr;
                } else {
                    const uint64_t now = static_cast<uint64_t>(wallMs);
                    document["lockTimeRemainingMs"] =
                        lockEnd > now ? lockEnd - now : 0;
                }
            } else {
                document["lockTimeRemainingMs"] = nullptr;
            }

            if (timingAvailable && breakActive && isTimeSynced && wallMs > 0 &&
                breakStart > 0) {
                const int64_t remaining = breakTimeRemaining(
                    breakStart, breakMax, static_cast<uint64_t>(wallMs));
                document["breakTimeRemainingMs"] = remaining > 0 ? remaining : 0;
            } else {
                document["breakTimeRemainingMs"] = nullptr;
            }
            break;
        }
        case radble::Surface::Button:
            document.add(JsonObject());
            document[0]["id"] = "enter";
            document[0]["pressed"] = digitalRead(Pins::Buttons::buttonEnter) == LOW;
            document.add(JsonObject());
            document[1]["id"] = "back";
            document[1]["pressed"] = digitalRead(Pins::Buttons::buttonBack) == LOW;
            document.add(JsonObject());
            document[2]["id"] = "power";
            document[2]["pressed"] = digitalRead(Pins::Buttons::buttonPowerDet) == LOW;
            break;
        case radble::Surface::Encoder:
            document["id"] = "encoder";
            document["value"] = rotaryEncoder.readEncoder();
            break;
        case radble::Surface::Magnetic:
            document["hall1"] = digitalRead(Pins::Magnetics::hall1) == LOW;
            document["hall2"] = digitalRead(Pins::Magnetics::hall2) == LOW;
            document["tmr1"] = digitalRead(Pins::Magnetics::tmr1) == LOW;
            document["tmr2"] = digitalRead(Pins::Magnetics::tmr2) == LOW;
            document["tmr3"] = digitalRead(Pins::Magnetics::tmr3) == LOW;
            document["paddleLockedRaw"] = isPaddleLocked();
            document["paddleLocked"] = getStablePaddleLocked();
            document["backplateClosed"] = isBackplateClosed();
            break;
        case radble::Surface::Power:
            document["usb"] = digitalRead(Pins::Power::usbDetect) == HIGH;
            document["charging"] = digitalRead(Pins::Power::usbDetect) == HIGH;
            document["available"] = inaReady;
            refreshPowerTelemetry();
            document["voltage"] = powerTelemetry.voltage;
            document["percent"] = powerTelemetry.percent;
            document["currentMa"] = powerTelemetry.currentMa;
            document["powerMw"] = powerTelemetry.powerMw;
            break;
        case radble::Surface::Motion:
            document["stateLocked"] = Motor::isStateLocked();
            document["paddleLocked"] = getStablePaddleLocked();
            document["calibrated"] = lkbx != nullptr && lkbx->motor.isCalibrated;
            if (xSemaphoreTake(preferencesMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                SessionTiming timing;
                if (preferences.getSessionTiming(timing)) {
                    document["lockStart"] = timing.lockStart;
                    document["lockEnd"] = timing.lockEnd;
                    document["breakActive"] = timing.breakActive;
                }
                xSemaphoreGive(preferencesMutex);
            }
            break;
        case radble::Surface::Connectivity: {
            const bool wifiConnected = WiFi.status() == WL_CONNECTED;
            document["wifi"] = wifiConnected;
            document["rssi"] = wifiConnected ? WiFi.RSSI() : 0;
            document["ip"] = WiFi.localIP().toString();
            document["mqtt"] = mqttConnected;
            document["server"] = isServerOnline(wifiConnected);
            document["clockSynced"] = isTimeSynced;
            document["wallMs"] = currentWallClockMs();
            break;
        }
        case radble::Surface::Indicator:
            document["id"] = "backlight";
            document["available"] = true;
            break;
        default:
            return "{}";
    }
    String output;
    serializeJson(document, output);
    return output;
}

radble::Result prepareOta(bool starting, const char*, void*) {
    if (!starting) return radble::Result::success();
    const String state = stateName();
    if (state.startsWith("locking") || state.startsWith("unlocking") ||
        state.startsWith("emergency.unlock"))
        return radble::Result::failure(
            "invalid_state", "Wait for lockbox motion to stop before direct OTA");
    const bool usbPowered = digitalRead(Pins::Power::usbDetect) == HIGH;
    if (!usbPowered && inaReady && ina.getBusVoltage_V() < 3.5f)
        return radble::Result::failure(
            "preflight_failed", "Battery is too low for direct OTA");
    Serial.println("[RAD BLE][LKBX] direct OTA safety state confirmed");
    return radble::Result::success();
}

void releaseDiagnosticOutputs(void*) { ledcWrite(0, 255); }

}  // namespace

radble::Server radBleServer;

bool initRadBle(NimBLEServer* server) {
    const radble::Config config = {
        .identity = {
            .deviceType = "LKBX",
            .deviceName = "LKBX",
            .serviceUuid = radble::LKBX_SERVICE_UUID,
            .firmwareVersion = currentVersionString,
            .build = FIRMWARE_BUILD_SHA,
            .partitionLayout = nullptr,
        },
        .capabilities = radble::CAP_BUTTON | radble::CAP_ENCODER |
                        radble::CAP_POWER | radble::CAP_MAGNETIC |
                        radble::CAP_MOTION | radble::CAP_CONNECTIVITY |
                        radble::CAP_INDICATOR | radble::CAP_SENSOR_STREAM,
        .channels = radble::CHANNEL_SENSOR_STREAM | radble::CHANNEL_BUTTON |
                    radble::CHANNEL_ENCODER | radble::CHANNEL_POWER |
                    radble::CHANNEL_MAGNETIC | radble::CHANNEL_MOTION |
                    radble::CHANNEL_CONNECTIVITY | radble::CHANNEL_INDICATOR |
                    radble::CHANNEL_BATTERY |
                    radble::CHANNEL_APPLICATION_OTA,
        .resources = RESOURCES,
        .resourceCount = sizeof(RESOURCES) / sizeof(RESOURCES[0]),
        .callbacks = {
            .commandHandler = handleCommand,
            .snapshotHandler = snapshot,
            .otaDataHandler = nullptr,
            .otaSafetyHandler = prepareOta,
            .leaseReleaseHandler = releaseDiagnosticOutputs,
            .streamSafetyHandler = nullptr,
        },
        .context = nullptr,
    };
    return radBleServer.begin(server, config);
}

bool stopBle() {
    if (!NimBLEDevice::isInitialized() || blePaused) return true;

    blePaused = true;
    bool stopped = true;

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (advertising != nullptr && advertising->isAdvertising())
        stopped = advertising->stop();

    NimBLEServer* server = NimBLEDevice::getServer();
    if (server != nullptr) {
        for (const uint16_t connection : server->getPeerDevices())
            stopped = server->disconnect(connection) && stopped;
    }

    if (stopped)
        ESP_LOGI(NIMBLE_TAG, "BLE service paused");
    else
        ESP_LOGE(NIMBLE_TAG, "Could not completely pause BLE service");
    return stopped;
}

bool startBle() {
    if (!blePaused) return true;
    if (!NimBLEDevice::isInitialized()) {
        ESP_LOGE(NIMBLE_TAG, "Could not resume uninitialized BLE service");
        return false;
    }

    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (advertising == nullptr) {
        ESP_LOGE(NIMBLE_TAG, "Could not find BLE advertiser to resume");
        return false;
    }

    blePaused = false;
    if (!advertising->isAdvertising() && !advertising->start()) {
        blePaused = true;
        ESP_LOGE(NIMBLE_TAG, "Could not resume BLE advertising");
        return false;
    }

    ESP_LOGI(NIMBLE_TAG, "BLE service resumed");
    return true;
}

bool isBlePaused() { return blePaused; }
