#pragma once

#include <cstddef>
#include <cstring>

namespace lockbox::security {

// WiFiManager registers every route it has, whatever the menu shows. The setup
// network is open, so anyone who joins it could otherwise reboot the box,
// erase its Wi-Fi credentials, or read the info page (which shows the station
// MAC). Setup needs only the root page, the network form, its save and exit.
// These routes are answered with 404 before WiFiManager's own handlers.
inline constexpr const char* PORTAL_REFUSED_ROUTES[] = {
    "/erase", "/restart", "/info", "/param", "/paramsave",
    "/close", "/status",  "/update", "/u",
};

inline constexpr std::size_t PORTAL_REFUSED_ROUTE_COUNT =
    sizeof(PORTAL_REFUSED_ROUTES) / sizeof(PORTAL_REFUSED_ROUTES[0]);

inline bool portalRouteRefused(const char* path) {
    if (path == nullptr) return true;
    for (const char* route : PORTAL_REFUSED_ROUTES) {
        if (std::strcmp(route, path) == 0) return true;
    }
    return false;
}

}  // namespace lockbox::security
