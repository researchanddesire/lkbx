#ifndef LOCKBOX_NETWORK_RECOVERY_H
#define LOCKBOX_NETWORK_RECOVERY_H

#include <stdint.h>

// A box can be cut off while its Wi-Fi link looks fine. On the 2026-11-16 rig
// run R8 ran out of internal RAM while several secure connections were being
// set up within two seconds. Afterwards the MQTT client could not connect any
// more, connections timed out and every name lookup failed, for 8 and for 40
// minutes, until the box was restarted. The station stayed associated the
// whole time and no disconnect event came, so the station keeper
// (utils/stationKeeper.h), which acts on a station that is down, never acted.
// Internal RAM stayed short and in pieces throughout (11.5 to 17.9 KB free,
// no block over 3.2 KB).
//
// So a box that was on the broker and has lost it, with its station still up,
// is checked and brought back in steps:
//
//   1. Off the broker for a minute: ask the dashboard whether it answers at
//      all (one plain HTTPS request). If it does, the network works and only
//      the broker is out of reach; nothing is done, and the question is asked
//      again five minutes later.
//   2. No answer either: the box is cut off. Stop the MQTT client, which
//      gives back its task stack and its TLS session, connect the Wi-Fi
//      station again, and start the client again.
//   3. Still off the broker two minutes after that, and the dashboard still
//      does not answer: restart the box, as soon as it is at rest. If that
//      takes more than a minute the dashboard is asked once more first.
//
// Being back on the broker ends all of it at any point.
//
// A restart is the last step because it is the only thing that has brought a
// cut-off box back. It is safe for the same reason waking from deep sleep is:
// the lock state, the session and its deadline are in flash and the boot
// sequence restores them. The caller still only restarts a box that is at
// rest (services/wm.cpp): on a page a restart leads back to, with nobody
// having touched it for a while, the motor off and no local session.
//
// Two rules keep this from restarting a box over and over when the fault is
// not the box's: none of it happens before the box has been on the broker
// once since it started, so a restart that did not help is not followed by
// another, and none of it happens while the station is down, which is the
// station keeper's business.
//
// This is the bookkeeping only; the caller asks the dashboard, connects
// again and restarts (services/wm.cpp). No Arduino dependencies, so it is
// unit-tested natively. See test/test_network_recovery.

namespace networkRecovery {
    // Off the broker with the station up for this long before the dashboard
    // is asked. The MQTT client tries again every ten seconds by itself.
    constexpr uint32_t kOffBrokerBeforeCheckMs = 60000;
    // The whole request to the dashboard, including its wait for the MQTT
    // client's own connection attempt (utils/tlsGate.h).
    constexpr int kCheckTimeoutMs = 15000;
    // After the Wi-Fi station was connected again, before a restart is
    // considered.
    constexpr uint32_t kAfterReconnectMs = 120000;
    // The dashboard answers and the broker does not: looked at again after.
    constexpr uint32_t kBrokerOnlyRecheckMs = 300000;
    // How long the box must have been at rest, without a break, right before
    // it restarts.
    constexpr uint32_t kAtRestMs = 5000;
    // A box that was busy may come to rest long after the dashboard last
    // failed to answer. Older than this, the question is asked once more
    // before the box restarts.
    constexpr uint32_t kNoAnswerFreshMs = 60000;
}  // namespace networkRecovery

enum class RecoveryStep : uint8_t {
    NONE,
    CHECK,      // ask the dashboard; report the outcome with checked()
    RECONNECT,  // stop MQTT, connect the Wi-Fi station again, start MQTT
    RESTART,    // restart the box now
};

struct RecoveryInputs {
    // The station has its link and its address.
    bool stationUp = false;
    // The box means to be on the broker: it has a client, and no Wi-Fi setup
    // portal holds it stopped.
    bool brokerExpected = false;
    // The MQTT client is connected.
    bool brokerUp = false;
    // Wi-Fi is somebody else's for now (a setup page is up).
    bool held = false;
    // The box is at rest and may restart. Only looked at while
    // restartWanted().
    bool atRest = false;
};

class NetworkRecovery {
  public:
    // Called about once a second, and not while a check is out.
    RecoveryStep next(const RecoveryInputs &in, uint32_t nowMs) {
        if (in.brokerUp) {
            onBrokerBefore_ = true;
            reset();
            return RecoveryStep::NONE;
        }
        if (!onBrokerBefore_ || !in.brokerExpected || in.held) {
            reset();
            return RecoveryStep::NONE;
        }

        switch (stage_) {
            case Stage::WATCHING:
                // The clock only runs while the station is up.
                if (!in.stationUp) {
                    counting_ = false;
                    return RecoveryStep::NONE;
                }
                if (!counting_) {
                    counting_ = true;
                    sinceMs_ = nowMs;
                    return RecoveryStep::NONE;
                }
                if (!elapsed(nowMs, networkRecovery::kOffBrokerBeforeCheckMs)) {
                    return RecoveryStep::NONE;
                }
                stage_ = Stage::FIRST_CHECK;
                return RecoveryStep::CHECK;

            case Stage::BROKER_ONLY:
                if (!in.stationUp) {
                    reset();
                    return RecoveryStep::NONE;
                }
                if (!elapsed(nowMs, networkRecovery::kBrokerOnlyRecheckMs)) {
                    return RecoveryStep::NONE;
                }
                stage_ = Stage::FIRST_CHECK;
                return RecoveryStep::CHECK;

            case Stage::RECONNECT_DUE:
                stage_ = Stage::AFTER_RECONNECT;
                sinceMs_ = nowMs;
                return RecoveryStep::RECONNECT;

            case Stage::AFTER_RECONNECT:
                // Connecting again takes the station down for a moment, and
                // it may stay down. The wait starts again when it is back.
                if (!in.stationUp) {
                    sinceMs_ = nowMs;
                    return RecoveryStep::NONE;
                }
                if (!elapsed(nowMs, networkRecovery::kAfterReconnectMs)) {
                    return RecoveryStep::NONE;
                }
                stage_ = Stage::SECOND_CHECK;
                return RecoveryStep::CHECK;

            case Stage::RESTART_WANTED:
                // A station that has dropped now is the station keeper's.
                // The box is looked at afresh once it is back.
                if (!in.stationUp) {
                    stage_ = Stage::AFTER_RECONNECT;
                    sinceMs_ = nowMs;
                    resting_ = false;
                    return RecoveryStep::NONE;
                }
                if (!in.atRest) {
                    resting_ = false;
                    return RecoveryStep::NONE;
                }
                if (!resting_) {
                    resting_ = true;
                    restingSinceMs_ = nowMs;
                    return RecoveryStep::NONE;
                }
                if (static_cast<uint32_t>(nowMs - restingSinceMs_) <
                    networkRecovery::kAtRestMs) {
                    return RecoveryStep::NONE;
                }
                // The box restarts on a "no answer" that is recent.
                if (elapsed(nowMs, networkRecovery::kNoAnswerFreshMs)) {
                    stage_ = Stage::SECOND_CHECK;
                    return RecoveryStep::CHECK;
                }
                stage_ = Stage::RESTARTING;
                return RecoveryStep::RESTART;

            case Stage::FIRST_CHECK:
            case Stage::SECOND_CHECK:
            case Stage::RESTARTING:
            default:
                return RecoveryStep::NONE;
        }
    }

    // The dashboard was asked. `answered`: it sent any HTTP response.
    void checked(bool answered, uint32_t nowMs) {
        if (stage_ != Stage::FIRST_CHECK && stage_ != Stage::SECOND_CHECK) {
            return;
        }
        if (answered) {
            stage_ = Stage::BROKER_ONLY;
            sinceMs_ = nowMs;
            return;
        }
        if (stage_ == Stage::FIRST_CHECK) {
            stage_ = Stage::RECONNECT_DUE;
            return;
        }
        stage_ = Stage::RESTART_WANTED;
        sinceMs_ = nowMs;
        resting_ = false;
    }

    // The dashboard has been asked at least once since the box was last on
    // the broker.
    bool stepped() const { return stage_ != Stage::WATCHING; }
    // A restart is waiting for the box to be at rest: the caller works out
    // RecoveryInputs::atRest.
    bool restartWanted() const { return stage_ == Stage::RESTART_WANTED; }

  private:
    enum class Stage : uint8_t {
        WATCHING,
        FIRST_CHECK,
        BROKER_ONLY,
        RECONNECT_DUE,
        AFTER_RECONNECT,
        SECOND_CHECK,
        RESTART_WANTED,
        RESTARTING,
    };

    void reset() {
        stage_ = Stage::WATCHING;
        counting_ = false;
        resting_ = false;
    }

    bool elapsed(uint32_t nowMs, uint32_t waitMs) const {
        // Unsigned subtraction keeps this correct across the millisecond
        // counter wrapping.
        return static_cast<uint32_t>(nowMs - sinceMs_) >= waitMs;
    }

    Stage stage_ = Stage::WATCHING;
    // The box has been on the broker since it started.
    bool onBrokerBefore_ = false;
    // Off the broker with the station up, and the clock is running.
    bool counting_ = false;
    // When the current wait began.
    uint32_t sinceMs_ = 0;
    // At rest on every pass since restingSinceMs_.
    bool resting_ = false;
    uint32_t restingSinceMs_ = 0;
};

#endif  // LOCKBOX_NETWORK_RECOVERY_H
