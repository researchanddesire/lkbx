#ifndef LOCKBOX_TLS_TURN_H
#define LOCKBOX_TLS_TURN_H

#include <cstdint>
#include <mutex>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "utils/tlsGate.h"

// One secure connection is set up at a time (utils/tlsGate.h has the reason
// and the rules). This is the part that waits: the MQTT client before it
// connects (MQTT_EVENT_BEFORE_CONNECT), the HTTP worker before it opens a
// request, and on staging builds the rig's connectivity probe.
//
// Header-only, and built on nothing but the timer and vTaskDelay, so the HTTP
// worker's host build (test/test_http_worker_security) compiles it as it is.

namespace tlsTurn {
    inline constexpr const char *kTag = "TlsTurn";
    inline std::mutex gateMutex;
    inline TlsGate gate;

    inline uint32_t nowMs() {
        return static_cast<uint32_t>(esp_timer_get_time() / 1000);
    }
}  // namespace tlsTurn

// `user` has connected, or failed to. Harmless when it holds no turn.
inline void endTlsTurn(TlsUser user) {
    std::lock_guard<std::mutex> lock(tlsTurn::gateMutex);
    tlsTurn::gate.give(user);
}

// `user` has connected and keeps its turn for `afterMs` more. Harmless when
// it holds no turn.
inline void endTlsTurnAfter(TlsUser user, uint32_t afterMs) {
    std::lock_guard<std::mutex> lock(tlsTurn::gateMutex);
    tlsTurn::gate.giveAfter(user, tlsTurn::nowMs(), afterMs);
}

// Waits until `user` may set up its secure connection: until nobody else is
// setting one up, for `waitMs` at most, or until `giveUp()` says the caller no
// longer wants to connect. True when the turn is the caller's, who ends it
// with endTlsTurn or endTlsTurnAfter. False when the wait ran out or was
// given up: the caller holds no turn and, if it still wants to, connects
// anyway.
template <typename GiveUp>
inline bool waitForTlsTurn(TlsUser user, uint32_t waitMs, GiveUp giveUp) {
    const uint32_t startedMs = tlsTurn::nowMs();
    for (;;) {
        const uint32_t now = tlsTurn::nowMs();
        const uint32_t waitedMs = now - startedMs;
        bool taken = false;
        TlsUser other = TlsUser::NONE;
        {
            std::lock_guard<std::mutex> lock(tlsTurn::gateMutex);
            taken = tlsTurn::gate.take(user, now);
            if (!taken) other = tlsTurn::gate.holder(now);
        }
        // The log macros are empty in the host build.
        (void)other;
        if (taken) {
            if (waitedMs > 0) {
                ESP_LOGI(tlsTurn::kTag,
                         "%s waited %u ms for another secure connection "
                         "before connecting",
                         tlsUserName(user), static_cast<unsigned>(waitedMs));
            }
            return true;
        }
        if (giveUp()) return false;
        if (waitedMs >= waitMs) {
            ESP_LOGW(tlsTurn::kTag,
                     "%s waited %u ms and %s is still connecting; going "
                     "ahead",
                     tlsUserName(user), static_cast<unsigned>(waitedMs),
                     tlsUserName(other));
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// A turn for as long as this object lives: taken (or waited out) where it is
// declared, given back when it goes out of scope.
class TlsTurn {
  public:
    template <typename GiveUp>
    TlsTurn(TlsUser user, uint32_t waitMs, GiveUp giveUp)
        : user_(user), held_(waitForTlsTurn(user, waitMs, giveUp)) {}

    TlsTurn(TlsUser user, uint32_t waitMs)
        : TlsTurn(user, waitMs, [] { return false; }) {}

    ~TlsTurn() {
        if (held_) endTlsTurn(user_);
    }

    TlsTurn(const TlsTurn &) = delete;
    TlsTurn &operator=(const TlsTurn &) = delete;

  private:
    TlsUser user_;
    bool held_;
};

#endif  // LOCKBOX_TLS_TURN_H
