#pragma once

#include <cstddef>
#include <cstdint>

namespace lockbox::security {

inline bool portalPeerAllowed(uint32_t localAddress, uint32_t apAddress,
                              uint32_t peerAddress,
                              const uint32_t* associatedAddresses,
                              std::size_t count, uint32_t stationAddress = 0) {
    if (!apAddress || localAddress != apAddress || !peerAddress ||
        !associatedAddresses || apAddress == stationAddress) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (associatedAddresses[i] == peerAddress) return true;
    }
    return false;
}

}  // namespace lockbox::security
