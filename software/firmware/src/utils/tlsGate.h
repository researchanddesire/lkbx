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
// An HTTPS request keeps its turn until its answer is read and its connection
// is closed, not only while it connects. Its session is as short as the
// request, and while it is open it holds internal RAM of its own; on the
// 2026-11-17 rig run the rig's probe connected beside an open request and a
// connected client during an unlock, and the client's next write found no
// encryption buffer. The MQTT client's session is different: it stays open
// for as long as the box is online and holds no turn once it has settled.
//
// Nobody waits for a turn forever: the caller waits a bounded time and then
// connects anyway (services/tlsTurn.h), which is what every connection did
// before. A turn that was never given back ends by itself. The rig's probe
// is the exception: it is an observer the firmware does not need, so without
// a turn, or with little internal RAM free, it does not connect at all and
// tries again a few seconds later (tlsProbeHasRoom).
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
    // The probe leaves internal RAM alone when little of it is free. Setting
    // up its connection takes internal RAM for the certificate chain and the
    // key exchange, and every encrypted record needs two 1.6 KB blocks the
    // encryption hardware can reach. On the 2026-11-17 rig run R8 had 39 KB
    // free in a 9.7 KB largest block with the Wi-Fi setup task running, and
    // was cut off at 13 KB in a 3 KB block and at 4 KB in a 1 KB block. Below
    // these the probe would be taking what the firmware's own connections
    // are about to need.
    constexpr uint32_t kProbeMinFreeBytes = 20 * 1024;
    constexpr uint32_t kProbeMinBlockBytes = 4 * 1024;
}  // namespace tlsGate

// Whether the rig's probe may set up its connection, given the internal RAM
// that is free now and the largest block of it.
inline bool tlsProbeHasRoom(uint32_t freeBytes, uint32_t largestBlockBytes) {
    return freeBytes >= tlsGate::kProbeMinFreeBytes &&
           largestBlockBytes >= tlsGate::kProbeMinBlockBytes;
}

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
