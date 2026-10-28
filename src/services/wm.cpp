#include "wm.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif_sta_list.h>

#include "PortalPeer.h"
#include "portal_guard.h"

#include "constants/strings.h"
#include "machine.h"
#include "services/serverOnline.h"

WiFiManager wm;

namespace {
bool isSetupClient(WebServer& server) {
    const auto client = server.client();
    const uint32_t apAddress = WiFi.softAPIP();
    if (!apAddress || uint32_t(client.localIP()) != apAddress) return false;

    // The IP stack accepts traffic addressed to either interface. Checking
    // only the destination IP would still admit routed station-LAN clients.
    // Require a currently associated AP client and its assigned DHCP address.
    wifi_sta_list_t wifiStations{};
    esp_netif_sta_list_t ipStations{};
    if (esp_wifi_ap_get_sta_list(&wifiStations) != ESP_OK ||
        esp_netif_get_sta_list(&wifiStations, &ipStations) != ESP_OK ||
        ipStations.num < 0 || ipStations.num > ESP_WIFI_MAX_CONN_NUM) return false;
    uint32_t addresses[ESP_WIFI_MAX_CONN_NUM]{};
    for (int i = 0; i < ipStations.num; ++i) addresses[i] = ipStations.sta[i].ip.addr;
    return lockbox::security::portalPeerAllowed(
        uint32_t(client.localIP()), apAddress, uint32_t(client.remoteIP()),
        addresses, std::size_t(ipStations.num),
        WiFi.status() == WL_CONNECTED ? uint32_t(WiFi.localIP()) : 0);
}

const IPAddress kPortalApIp(192, 168, 4, 1);
const IPAddress kPortalGateway(192, 168, 4, 1);
const IPAddress kPortalSubnet(255, 255, 255, 0);
constexpr char kPortalFallbackHtml[] PROGMEM = R"html(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta http-equiv="refresh" content="0; url=/">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Lockbox Setup</title>
</head>
<body>
  <p>Redirecting to Lockbox Wi-Fi setup...</p>
  <p>If this page does not open automatically, go to <a href="http://192.168.4.1">http://192.168.4.1</a>.</p>
</body>
</html>
)html";

void registerCaptivePortalFallbackRoutes() {
    if (!wm.server) {
        return;
    }

    auto sendPortalFallback = []() {
        wm.server->sendHeader("Cache-Control", "no-store");
        wm.server->sendHeader("Connection", "close");
        wm.server->send_P(200, "text/html", kPortalFallbackHtml);
    };

    // Captive-portal probes vary by platform. Route the common endpoints
    // back to the portal page so users land on setup quickly.
    wm.server->on("/generate_204", sendPortalFallback);
    wm.server->on("/gen_204", sendPortalFallback);
    wm.server->on("/hotspot-detect.html", sendPortalFallback);
    wm.server->on("/connecttest.txt", sendPortalFallback);
    wm.server->on("/ncsi.txt", sendPortalFallback);

    // Fallback for unexpected paths while the AP portal is active.
    wm.server->onNotFound(sendPortalFallback);
}
}  // namespace

void initWM() {
    wm.setDebugOutput(false);
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) {
        clearServerOnline();
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    // Leave WiFi.persistent at its default (true) so WiFiManager's
    // post-portal WiFi.begin(ssid, pass) writes credentials to NVS and
    // they survive reboots. Setting persistent(false) here was the bug
    // that caused the captive portal to reappear on every boot.
    WiFi.begin();
    wm.setCleanConnect(true);
    const char *portalMenu[] = {"wifinoscan", "info", "exit"};
    wm.setMenu(portalMenu, sizeof(portalMenu) / sizeof(portalMenu[0]));
    wm.setShowInfoUpdate(false);
    // WiFiManager installs its upload handlers after this callback. WebServer
    // selects the first matching handler, including multipart upload callbacks.
    wm.setWebServerCallback([]() {
        wm.server->addHandler(new PortalRequestGuard(*wm.server, isSetupClient));
        auto denied = []() {
            wm.server->send(403, "text/plain", "Use the verified firmware updater");
        };
        wm.server->on("/update", HTTP_ANY, denied, []() {});
        wm.server->on("/u", HTTP_ANY, denied, []() {});
    });
    wm.setAPStaticIPConfig(kPortalApIp, kPortalGateway, kPortalSubnet);
    wm.setCaptivePortalEnable(true);
    wm.setClass(Str::Wifi::WM_CLASS);
    wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(400);
    wm.setSaveConfigCallback(
        []() { lkbx->sm->process_event(WifiConnected{}); });
}

void processWifiTask(void *pvParameters) {
    auto isInCorrectState = []() {
        return lkbx->sm->is("init.preflight.wifiSetup"_s) ||
               lkbx->sm->is("pairing.wifi"_s) ||
               lkbx->sm->is("lockMenu.wifi"_s) ||
               lkbx->sm->is("emergency.wifi"_s) || lkbx->sm->is("wmConfig"_s) ||
               lkbx->sm->is("update.wifi"_s);
    };
    // The setup network is intentionally open on every track; the portal
    // guard still admits only clients associated with this access point.
    wm.startConfigPortal(Str::Wifi::PORTAL_SSID);
    registerCaptivePortalFallbackRoutes();
    wm.process();

    while (isInCorrectState()) {
        wm.process();
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    // Guard: only stop the portal if it's still active.
    // WiFiManager may have already torn down its web server
    // after a successful connection, so calling stop blindly
    // would dereference a null pointer.
    if (wm.getConfigPortalActive()) {
        wm.stopConfigPortal();
        wm.process();
    }
    vTaskDelete(nullptr);
}
