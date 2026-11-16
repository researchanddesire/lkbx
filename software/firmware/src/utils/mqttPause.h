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
// The client is stopped before the portal's own task is created, so that task
// never has to fit beside it (services/wm.cpp).
//
// The same retry starts a client whose task could not be created at boot
// (startFailed): the box keeps trying every two seconds instead of staying
// off the broker until it restarts.
//
// While the client is stopped the box hears nothing the dashboard publishes,
// so after it has connected again the box reads its state from the dashboard
// once (utils/stateRead.h).
//
// This is the bookkeeping only: what is paused, and what to do next. The
// caller stops and starts the client (services/mqtt.cpp). No Arduino
// dependencies, so it is unit-tested natively. See test/test_mqtt_pause.

namespace mqttPause {
    // How long after the portal closes, and between failed attempts, the
    // client is started. Its task needs one free 10 KB block of internal RAM,
    // which the closing portal gives back over a moment.
    constexpr uint32_t kStartRetryMs = 2000;
}  // namespace mqttPause

enum class MqttPauseStep : uint8_t {
    NONE,
    START_CLIENT,  // the portal has closed: start the client again
    RECONCILE,     // it is connected again: ask for the dashboard's state
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
        return reconcile_ && connected ? MqttPauseStep::RECONCILE
                                       : MqttPauseStep::NONE;
    }

    // The caller tried to start the client.
    void startTried(bool started, uint32_t nowMs) {
        waitFromMs_ = nowMs;
        if (!started) return;
        paused_ = false;
        reconcile_ = true;
    }

    // The client exists but could not be started, with no portal involved:
    // its task did not fit in memory at boot. It is started like a client
    // stopped for a portal, and its state is read once it has connected.
    void startFailed(uint32_t nowMs) {
        paused_ = true;
        reconcile_ = false;
        waitFromMs_ = nowMs;
    }

    // The caller asked for the state read. Asking cannot fail: the read is
    // tried until it completes (utils/stateRead.h).
    void reconcileAsked() { reconcile_ = false; }

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

    // Portals open or being opened. They are opened one at a time
    // (services/wm.cpp); counted so that a late close of one can never start
    // the client under the next.
    uint8_t portals_ = 0;
    // The client is not running: stopped for a portal, or never started.
    bool paused_ = false;
    // It was started again and the state has not been read since.
    bool reconcile_ = false;
    // When the last portal closed, or the last attempt was made.
    uint32_t waitFromMs_ = 0;
};

#endif  // LOCKBOX_MQTT_PAUSE_H
