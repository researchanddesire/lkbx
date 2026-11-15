#ifndef LOCKBOX_MQTT_PAUSE_H
#define LOCKBOX_MQTT_PAUSE_H

#include <stdint.h>

// The Wi-Fi setup portal and the MQTT client do not fit in internal RAM
// together on R8 hardware. With both up (2026-11-15 rig run) free internal RAM
// fell to 183 bytes: TLS handshakes failed for lack of memory and the
// Bluetooth link dropped. So the client is stopped while a portal is open,
// which gives back its 10 KB task stack and its TLS session, and started again
// once the last portal has closed. Each start is a new connection, with its
// own sealed last will.
//
// While the client is stopped the box hears nothing the dashboard publishes,
// so after it has connected again the box reads its state from the dashboard
// once, as it does after any reconnect at boot.
//
// This is the bookkeeping only: what is paused, and what to do next. The
// caller stops and starts the client (services/mqtt.cpp). No Arduino
// dependencies, so it is unit-tested natively. See test/test_mqtt_pause.

namespace mqttPause {
    // How long after the portal closes, and between failed attempts, the
    // client is started. Its task needs one free 10 KB block of internal RAM,
    // which the closing portal gives back over a moment.
    constexpr uint32_t kStartRetryMs = 2000;
    // Between attempts to start the state pull, and how many are made.
    constexpr uint32_t kReconcileRetryMs = 5000;
    constexpr uint8_t kReconcileAttempts = 6;
}  // namespace mqttPause

enum class MqttPauseStep : uint8_t {
    NONE,
    START_CLIENT,  // the portal has closed: start the client again
    RECONCILE,     // it is connected again: read the dashboard's state
};

class MqttPause {
  public:
    // A portal is about to open.
    void portalOpened() {
        if (portals_ < UINT8_MAX) ++portals_;
    }

    // Whether the caller must stop the client now.
    bool shouldStop() const { return portals_ > 0 && !paused_; }

    // The caller stopped the client.
    void stopped() {
        paused_ = true;
        reconcile_ = false;
    }

    // A portal has closed. The client starts again after the last one.
    void portalClosed(uint32_t nowMs) {
        if (portals_ > 0) --portals_;
        if (portals_ == 0 && paused_) waitFromMs_ = nowMs;
    }

    MqttPauseStep next(uint32_t nowMs, bool connected) const {
        if (paused_) {
            if (portals_ > 0) return MqttPauseStep::NONE;
            return elapsed(nowMs, mqttPause::kStartRetryMs)
                       ? MqttPauseStep::START_CLIENT
                       : MqttPauseStep::NONE;
        }
        if (!reconcile_ || !connected) return MqttPauseStep::NONE;
        const bool due = reconcileAttempts_ == 0 ||
                         elapsed(nowMs, mqttPause::kReconcileRetryMs);
        return due ? MqttPauseStep::RECONCILE : MqttPauseStep::NONE;
    }

    // The caller tried to start the client.
    void startTried(bool started, uint32_t nowMs) {
        waitFromMs_ = nowMs;
        if (!started) return;
        paused_ = false;
        reconcile_ = true;
        reconcileAttempts_ = 0;
    }

    // The caller tried to start the state pull.
    void reconcileTried(bool started, uint32_t nowMs) {
        waitFromMs_ = nowMs;
        if (reconcileAttempts_ < UINT8_MAX) ++reconcileAttempts_;
        if (started || reconcileAttempts_ >= mqttPause::kReconcileAttempts) {
            reconcile_ = false;
        }
    }

    // The client no longer exists (a firmware update removes it).
    void clientGone() {
        paused_ = false;
        reconcile_ = false;
    }

    bool paused() const { return paused_; }
    bool portalOpen() const { return portals_ > 0; }

  private:
    bool elapsed(uint32_t nowMs, uint32_t waitMs) const {
        return static_cast<uint32_t>(nowMs - waitFromMs_) >= waitMs;
    }

    // Portal tasks alive. Two can overlap for a moment when a portal page is
    // left and entered again.
    uint8_t portals_ = 0;
    // The client is stopped for a portal.
    bool paused_ = false;
    // It was started again and the state has not been read since.
    bool reconcile_ = false;
    uint8_t reconcileAttempts_ = 0;
    // When the last portal closed, or the last attempt was made.
    uint32_t waitFromMs_ = 0;
};

#endif  // LOCKBOX_MQTT_PAUSE_H
