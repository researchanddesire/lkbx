#ifndef LOCKBOX_TLS_GATE_H
#define LOCKBOX_TLS_GATE_H

#include <stdint.h>

// Setting up a secure connection is the most memory a connection ever needs:
// the certificate chain, the key exchange and the first encrypted records all
// take internal RAM at once. R8 hardware does not have enough of it for two
// at the same time. On the 2026-11-16 rig run the MQTT client connected again
// as the Wi-Fi setup page closed, the state read opened its own connection
// within the same two seconds, and in 5 of 7 runs an encryption buffer could
// not be allocated (`esp-aes: Failed to allocate memory`): the MQTT connection
// dropped, and twice the box then stayed without network until it was
// restarted.
//
// So secure connections are set up one at a time. Whoever is about to connect
// (the MQTT client, the HTTP worker, the rig's connectivity probe on staging
// builds) takes a turn first and gives it back once its connection is up or
// has failed. The MQTT client keeps its turn for a moment after connecting,
// because it sends its subscription, its provenance record and any waiting
// reports right away.
//
// Nobody waits for a turn forever: the caller waits a bounded time and then
// connects anyway (services/tlsTurn.h), which is what every connection did
// before. A turn that was never given back ends by itself.
//
// This is the bookkeeping only. No Arduino dependencies, so it is unit-tested
// natively. See test/test_tls_gate.

namespace tlsGate {
    // A turn nobody gave back ends after this long. Longer than the MQTT
    // client's own connection timeout (10 s).
    constexpr uint32_t kLeaseMs = 15000;
    // How long the MQTT client keeps its turn after it has connected.
    constexpr uint32_t kMqttSettleMs = 1500;
    // The longest the MQTT client waits for an HTTPS connection being set
    // up. It waits on its own task while that holds the client's lock, as it
    // does for the whole of a connection attempt: a report another task
    // queues at that moment waits with it. So this stays short.
    constexpr uint32_t kMqttWaitMs = 4000;
    // The longest an HTTPS request waits for the MQTT client, which connects
    // in about a second when the network is well. A request never spends
    // more than a third of the time it has left on waiting, so one that
    // meets a client that cannot connect still has time for its own work.
    constexpr uint32_t kHttpWaitMs = 4000;
    // The rig's probe (staging builds) is the least urgent.
    constexpr uint32_t kProbeWaitMs = 10000;
}  // namespace tlsGate

enum class TlsUser : uint8_t {
    NONE,
    MQTT,
    HTTP,
    PROBE,
};

inline const char *tlsUserName(TlsUser user) {
    switch (user) {
        case TlsUser::MQTT:
            return "MQTT";
        case TlsUser::HTTP:
            return "HTTPS";
        case TlsUser::PROBE:
            return "the rig probe";
        case TlsUser::NONE:
        default:
            return "nobody";
    }
}

class TlsGate {
  public:
    // `user` wants to set up a secure connection now. True when the turn is
    // its own: nobody else holds one, or the holder's turn has run out. The
    // holder taking it again starts a new turn.
    bool take(TlsUser user, uint32_t nowMs) {
        if (user == TlsUser::NONE) return false;
        if (holder_ != user && held(nowMs)) return false;
        holder_ = user;
        sinceMs_ = nowMs;
        forMs_ = tlsGate::kLeaseMs;
        return true;
    }

    // `user` is done: connected, or failed. Somebody else's turn is left
    // alone, so a caller that went ahead without one can still call this.
    void give(TlsUser user) {
        if (holder_ == user) holder_ = TlsUser::NONE;
    }

    // `user` is connected but keeps its turn for `afterMs` more.
    void giveAfter(TlsUser user, uint32_t nowMs, uint32_t afterMs) {
        if (holder_ != user) return;
        sinceMs_ = nowMs;
        forMs_ = afterMs;
    }

    // Who holds the turn now, or NONE.
    TlsUser holder(uint32_t nowMs) const {
        return held(nowMs) ? holder_ : TlsUser::NONE;
    }

  private:
    bool held(uint32_t nowMs) const {
        // Unsigned subtraction keeps this correct across the millisecond
        // counter wrapping.
        return holder_ != TlsUser::NONE &&
               static_cast<uint32_t>(nowMs - sinceMs_) < forMs_;
    }

    TlsUser holder_ = TlsUser::NONE;
    // When the turn was taken, or last extended.
    uint32_t sinceMs_ = 0;
    // How long it lasts from then, unless it is given back first.
    uint32_t forMs_ = tlsGate::kLeaseMs;
};

#endif  // LOCKBOX_TLS_GATE_H
