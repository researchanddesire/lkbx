#include <unity.h>
#include <PortalPeer.h>
#include <PortalRoutes.h>
#include "WebServer.h"
#include "../../src/services/portal_guard.h"

using lockbox::security::portalPeerAllowed;

void setUp() {}
void tearDown() {}

namespace {
constexpr uint32_t ap = 0x0104a8c0;
constexpr uint32_t setupClient = 0x0204a8c0;
constexpr uint32_t lan = 0x0201a8c0;
uint32_t local = ap;
uint32_t remote = setupClient;
uint32_t stations[] = {setupClient};
std::size_t stationCount = 1;

bool allowed(WebServer&) {
    return portalPeerAllowed(local, ap, remote, stations, stationCount);
}

class MutatingHandler : public RequestHandler {
 public:
    int writes = 0;
    int uploads = 0;
    bool canHandle(HTTPMethod, String) override { return true; }
    bool canUpload(String) override { return true; }
    bool handle(WebServer&, HTTPMethod, String) override { ++writes; return true; }
    void upload(WebServer&, String, HTTPUpload&) override { ++uploads; }
};

void dispatch(WebServer& server, PortalRequestGuard& guard, MutatingHandler& downstream,
              HTTPMethod method, const String& path, bool multipart = false) {
    RequestHandler* selected = guard.canHandle(method, path)
                                   ? static_cast<RequestHandler*>(&guard) : &downstream;
    if (multipart && selected->canUpload(path)) {
        HTTPUpload upload;
        selected->upload(server, path, upload);
    }
    selected->handle(server, method, path);
}
}  // namespace

void only_associated_setup_clients_are_allowed() {
    TEST_ASSERT_TRUE(portalPeerAllowed(ap, ap, setupClient, stations, 1));
    TEST_ASSERT_FALSE(portalPeerAllowed(lan, ap, setupClient, stations, 1));
    TEST_ASSERT_FALSE(portalPeerAllowed(ap, ap, lan, stations, 1));
    TEST_ASSERT_FALSE(portalPeerAllowed(ap, ap, setupClient, stations, 0));
    TEST_ASSERT_FALSE(portalPeerAllowed(ap, ap, setupClient, nullptr, 1));
    TEST_ASSERT_FALSE(portalPeerAllowed(0, 0, setupClient, stations, 1));
    TEST_ASSERT_FALSE(portalPeerAllowed(ap, ap, 0, stations, 1));
    // An identical station/AP address cannot identify the accepting interface.
    TEST_ASSERT_FALSE(portalPeerAllowed(ap, ap, setupClient, stations, 1, ap));
}

void lan_requests_never_reach_save_reset_erase_or_upload() {
    WebServer server;
    PortalRequestGuard guard(server, allowed);
    MutatingHandler downstream;
    local = ap;
    remote = lan; // Includes station-LAN traffic routed to the AP address.
    stationCount = 1;
    for (const auto* path : {"/", "/wifisave", "/r", "/erase", "/update", "/u"}) {
        dispatch(server, guard, downstream, HTTP_POST, path, true);
        TEST_ASSERT_EQUAL(403, server.status);
    }
    TEST_ASSERT_EQUAL(0, downstream.writes);
    TEST_ASSERT_EQUAL(0, downstream.uploads);

    remote = setupClient;
    local = lan;
    dispatch(server, guard, downstream, HTTP_GET, "/wifisave");
    TEST_ASSERT_EQUAL(0, downstream.writes);
    local = ap;
    stationCount = 0; // A disconnected client or a failed lookup is denied.
    dispatch(server, guard, downstream, HTTP_POST, "/wifisave");
    TEST_ASSERT_EQUAL(0, downstream.writes);
}

void authorized_setup_flow_proceeds_and_missing_policy_denies() {
    WebServer server;
    PortalRequestGuard guard(server, allowed);
    MutatingHandler downstream;
    local = ap;
    remote = setupClient;
    stationCount = 1;
    dispatch(server, guard, downstream, HTTP_POST, "/wifisave");
    TEST_ASSERT_EQUAL(1, downstream.writes);
    PortalRequestGuard unconfigured(server, nullptr);
    dispatch(server, unconfigured, downstream, HTTP_POST, "/u", true);
    TEST_ASSERT_EQUAL(403, server.status);
    TEST_ASSERT_EQUAL(1, downstream.writes);
    TEST_ASSERT_EQUAL(0, downstream.uploads);
}

void setup_pages_stay_and_reset_erase_info_and_upload_are_refused() {
    using lockbox::security::portalRouteRefused;
    for (const char* path : {"/", "/wifi", "/0wifi", "/wifisave", "/exit"}) {
        TEST_ASSERT_FALSE_MESSAGE(portalRouteRefused(path), path);
    }
    for (const char* path : {"/erase", "/restart", "/info", "/param", "/paramsave",
                             "/close", "/status", "/update", "/u"}) {
        TEST_ASSERT_TRUE_MESSAGE(portalRouteRefused(path), path);
    }
    TEST_ASSERT_TRUE(portalRouteRefused(nullptr));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(only_associated_setup_clients_are_allowed);
    RUN_TEST(lan_requests_never_reach_save_reset_erase_or_upload);
    RUN_TEST(authorized_setup_flow_proceeds_and_missing_policy_denies);
    RUN_TEST(setup_pages_stay_and_reset_erase_info_and_upload_are_refused);
    return UNITY_END();
}
