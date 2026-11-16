#ifndef LOCKBOX_MQTT_PAUSE_H
#define LOCKBOX_MQTT_PAUSE_H

#include <stdint.h>

// The Wi-Fi setup portal and a connected MQTT client do not fit in internal
// RAM together on R8 hardware. With both up (2026-11-15 rig run) free internal
// RAM fell to 183 bytes: TLS handshakes failed for lack of memory and the
// Bluetooth link dropped. So the client is taken off the broker while a
// portal is open and connects again once the last portal has closed. Each
// connection is a new one, with its own sealed last will.
//
// "Paused" means the client's connection is closed, which gives back its TLS
// session, while its task is kept (services/mqtt.cpp). The task and its 10 KB
// stack used to be given back too, and then had to be created again after the
// portal: on R8 that block was not always there in one piece, and the box
// stayed off the broker for good (2026-11-16 rig run). The task is only
// stopped as well when the portal's own task cannot be created beside it.
//
// The client is paused before the portal's own task is created, so that task
// never has to fit beside a connected client (services/wm.cpp).
//
// The same retry starts a client whose task could not be created at boot
// (startFailed), or was stopped after all: the box keeps trying every two
// seconds instead of staying off the broker until it restarts. A client that
// cannot be started for two minutes is a fault of the box's own, and network
// recovery restarts the box when it is at rest (utils/networkRecovery.h).
//
// While the client is paused the box hears nothing the dashboard publishes,
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

// The client also connects again by itself, without having been stopped: the
// broker or the network dropped the connection and the client's own retry
// brought it back. The broker keeps nothing for a client that was away, so a
// command published in between is never delivered (2026-11-16 rig run: the
// broker answered `no_matching_subscribers` for one sent during a short
// drop). So the state is read after every connection that follows a drop, not
// only after a start.
//
// The first connection since the box started is not one of these: the boot
// sequence reads the state itself.
class MqttReconnect {
  public:
    // Called often with whether the client is connected now. True once for
    // every connection after the first.
    bool seen(bool connected) {
        const bool again = connected && !connected_ && connectedBefore_;
        if (connected) connectedBefore_ = true;
        connected_ = connected;
        return again;
    }

  private:
    bool connected_ = false;
    bool connectedBefore_ = false;
};

#endif  // LOCKBOX_MQTT_PAUSE_H
