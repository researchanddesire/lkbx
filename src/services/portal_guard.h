#pragma once

#include <WebServer.h>

// Registered before every WiFiManager route. WebServer chooses its handler
// before parsing multipart bodies, so rejected peers never reach upload or
// credential-changing callbacks.
class PortalRequestGuard final : public RequestHandler {
 public:
    using AllowPeer = bool (*)(WebServer&);
    PortalRequestGuard(WebServer& server, AllowPeer allowPeer)
        : server_(server), allowPeer_(allowPeer) {}

    bool canHandle(HTTPMethod, String) override {
        return !allowPeer_ || !allowPeer_(server_);
    }

    bool handle(WebServer& server, HTTPMethod, String) override {
        server.send(403, "text/plain", "Connect to the Lockbox setup network");
        return true;
    }

 private:
    WebServer& server_;
    AllowPeer allowPeer_;
};
