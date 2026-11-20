#ifndef LOCKBOX_STATION_KEEPER_H
#define LOCKBOX_STATION_KEEPER_H

#include <stdint.h>

// Nothing in the firmware brought the Wi-Fi station back once it had dropped:
// it relied on the Arduino core, which connects again once per disconnect
// event. That leaves the box offline until it restarts when
//   - that one attempt fails without a further event (no memory for it, at
//     the moment internal RAM is shortest),
//   - the access point sends the station away with "leaving" (reason 8),
//     which the core takes for the box's own wish and does not answer, or
//   - the setup portal was opened while the box was offline: WiFiManager
//     turns the station off for it, and on closing reconnects only from a
//     state the core does not report there.
// On the 2026-11-16 rig run R8 lost its network for over four minutes, until
// it was restarted, while other devices on the same access point were fine.
//
// So a station that has stayed down for 30 seconds is begun again with its
// saved credentials, and again every 30 seconds while it stays down. Never
// while the setup portal is open or being opened: a station that scans takes
// the portal's access point off its channel.
//
// This is the bookkeeping only; the caller begins the station
// (services/wm.cpp). No Arduino dependencies, so it is unit-tested natively.
// See test/test_station_keeper.

namespace stationKeeper {
    constexpr uint32_t kDownBeforeBeginMs = 30000;
}  // namespace stationKeeper

class StationKeeper {
  public:
    // Called often. `connected`: the station has its link and address.
    // `held`: the station must be left alone now (a portal). True when the
    // caller should begin the station again.
    bool due(bool connected, bool held, uint32_t nowMs) {
        if (connected || held) {
            down_ = false;
            return false;
        }
        if (!down_) {
            down_ = true;
            sinceMs_ = nowMs;
            return false;
        }
        if (static_cast<uint32_t>(nowMs - sinceMs_) <
            stationKeeper::kDownBeforeBeginMs) {
            return false;
        }
        // The next attempt waits as long again.
        sinceMs_ = nowMs;
        return true;
    }

  private:
    bool down_ = false;
    // When the station was first seen down, or last begun.
    uint32_t sinceMs_ = 0;
};

#endif  // LOCKBOX_STATION_KEEPER_H
