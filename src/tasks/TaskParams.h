#ifndef LOCKBOX_TASKPARAMS_H
#define LOCKBOX_TASKPARAMS_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct TaskParams {
    const char *name;
    uint32_t stackSize;
    UBaseType_t priority;
    BaseType_t core;
};

// ── main.cpp ────────────────────────────────────────────────────────────────
namespace Tasks {

    constexpr TaskParams wifi = {
        .name = "wifiTask",
        .stackSize = 10 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams button = {
        .name = "buttonTask",
        .stackSize = 8 * configMINIMAL_STACK_SIZE,
        .priority = configMAX_PRIORITIES - 1,
        .core = 0,
    };

    constexpr TaskParams idle = {
        .name = "idleTask",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 0,
    };

    constexpr TaskParams activityLog = {
        .name = "activityLogTask",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 0,
    };

    constexpr TaskParams taskMonitor = {
        .name = "taskMonitor",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 0,
        .core = 1,
    };

    // ── services
    // ────────────────────────────────────────────────────────────────

    constexpr TaskParams nimble = {
        .name = "nimbleTask",
        .stackSize = 8 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams httpWorker = {
        .name = "httpWorker",
        .stackSize = 16 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 0,
    };

    constexpr TaskParams hourlyWeb = {
        .name = "hourlyWebTasks",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 0,
    };

    constexpr TaskParams mqttHello = {
        .name = "mqttHello",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 0,
    };

    constexpr TaskParams headerBar = {
        .name = "headerBarTask",
        .stackSize = 4 * configMINIMAL_STACK_SIZE,
        .priority = tskIDLE_PRIORITY,
        .core = 1,
    };

    // ── motor
    // ───────────────────────────────────────────────────────────────────

    constexpr TaskParams currentWatch = {
        .name = "currentWatch",
        .stackSize = 5 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 0,
    };

    // ── state machine UI tasks
    // ──────────────────────────────────────────────────

    constexpr TaskParams drawHello = {
        .name = "drawHello",
        .stackSize = 4 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams preflightScreen = {
        .name = "preflightScreen",
        .stackSize = 4 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams unlockTimer = {
        .name = "unlockTimer",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams drawLockAnim = {
        .name = "drawLockAnim",
        .stackSize = 4 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams loader = {
        .name = "loaderTask",
        .stackSize = 5 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams loadingAnim = {
        .name = "loadingAnim",
        .stackSize = 5 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams breakTimer = {
        .name = "breakTimer",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams drawMenu = {
        .name = "drawMenu",
        .stackSize = 3 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams timerConfig = {
        .name = "timerConfig",
        .stackSize = 8 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams delayDispatch = {
        .name = "delayDispatch",
        // Done runs locking preflight and its nested Done synchronously,
        // including the MQTT confirmation and provenance/NVS logging.
        // The former stack overflowed when restoring a saved lock.
        .stackSize = 12 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    // ── state machine async tasks
    // ───────────────────────────────────────────────

    constexpr TaskParams calibrate = {
        .name = "calibrate",
        .stackSize = 8 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams checkWifi = {
        .name = "checkWifi",
        .stackSize = 5 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams checkUpdates = {
        .name = "checkUpdates",
        // HTTPS response parsing exceeded the former stack on ESP32-S3 during
        // the onboarding update check (stack-canary reset in checkUpdates).
        .stackSize = 12 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams pairingCheck = {
        .name = "pairingCheck",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams syncLockState = {
        .name = "syncLockState",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams syncClock = {
        .name = "syncClock",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams wifiConnect = {
        .name = "wifiConnect",
        .stackSize = 6 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

    constexpr TaskParams mqttConnect = {
        .name = "mqttConnect",
        .stackSize = 8 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 0,
    };

    constexpr TaskParams update = {
        .name = "updateTask",
        .stackSize = 32 * configMINIMAL_STACK_SIZE,
        .priority = 1,
        .core = 1,
    };

}  // namespace Tasks

// ── Task handles (defined in TaskParams.cpp) ────────────────────────────────
namespace TaskHandles {

    // main.cpp
    extern TaskHandle_t wifi;
    extern TaskHandle_t button;
    extern TaskHandle_t idle;
    extern TaskHandle_t activityLog;
    extern TaskHandle_t taskMonitor;

    // services
    extern TaskHandle_t nimble;
    extern TaskHandle_t httpWorker;
    extern TaskHandle_t hourlyWeb;
    extern TaskHandle_t mqttHello;
    extern TaskHandle_t headerBar;

    // motor
    extern TaskHandle_t currentWatch;

    // state machine UI
    extern TaskHandle_t drawHello;
    extern TaskHandle_t drawMenu;
    extern TaskHandle_t unlockTimer;
    extern TaskHandle_t drawLockAnim;
    extern TaskHandle_t loader;
    extern TaskHandle_t loadingAnim;
    extern TaskHandle_t breakTimer;
    extern TaskHandle_t preflightScreen;
    extern TaskHandle_t timerConfig;
    extern TaskHandle_t delayDispatch;

    // state machine async
    extern TaskHandle_t calibrate;
    extern TaskHandle_t checkWifi;
    extern TaskHandle_t checkUpdates;
    extern TaskHandle_t pairingCheck;
    extern TaskHandle_t syncLockState;
    extern TaskHandle_t syncClock;
    extern TaskHandle_t wifiConnect;
    extern TaskHandle_t mqttConnect;
    extern TaskHandle_t update;

}  // namespace TaskHandles

#endif  // LOCKBOX_TASKPARAMS_H
