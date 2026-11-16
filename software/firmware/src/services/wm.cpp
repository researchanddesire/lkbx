#include "wm.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif_sta_list.h>

#include <atomic>

#include "PortalPeer.h"
#include "PortalRoutes.h"
#include "portal_guard.h"

#include "constants/LogTags.h"
#include "constants/strings.h"
#include "machine.h"
#include "services/mqtt.h"
#include "services/serverOnline.h"
#include "tasks/TaskStart.h"

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
    WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t info) {
        clearServerOnline();
        // Why the station dropped, with the memory picture: a box that lost
        // its network for minutes (2026-11-16 rig run on R8) left nothing in
        // the log to say whether the link itself had gone. At most one line
        // every ten seconds, since a network that is down repeats the event.
        static uint32_t loggedMs = 0;
        static bool logged = false;
        const uint32_t now = millis();
        if (logged && now - loggedMs < 10000) return;
        logged = true;
        loggedMs = now;
        ESP_LOGW(WIFI_CONNECT_TAG,
                 "Wi-Fi station disconnected (reason %u): largest free "
                 "internal block %u bytes, %u bytes free",
                 static_cast<unsigned>(info.wifi_sta_disconnected.reason),
                 static_cast<unsigned>(
                     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    // Leave WiFi.persistent at its default (true) so WiFiManager's
    // post-portal WiFi.begin(ssid, pass) writes credentials to NVS and
    // they survive reboots. Setting persistent(false) here was the bug
    // that caused the captive portal to reappear on every boot.
    WiFi.begin();
    wm.setCleanConnect(true);
    // No "info" page: it shows the station MAC, which identifies the box to
    // the dashboard and broker, to anyone who joins the open setup network.
    const char *portalMenu[] = {"wifinoscan", "exit"};
    wm.setMenu(portalMenu, sizeof(portalMenu) / sizeof(portalMenu[0]));
    wm.setShowInfoUpdate(false);
    // WiFiManager installs its upload handlers after this callback. WebServer
    // selects the first matching handler, including multipart upload callbacks.
    wm.setWebServerCallback([]() {
        wm.server->addHandler(new PortalRequestGuard(*wm.server, isSetupClient));
        // Firmware installs only through the verified updater, and the reset,
        // erase and info pages are not part of setup (PortalRoutes.h).
        auto refused = []() {
            wm.server->sendHeader("Connection", "close");
            wm.server->send(404, "text/plain", "Not available");
        };
        for (const char *route : lockbox::security::PORTAL_REFUSED_ROUTES) {
            wm.server->on(route, HTTP_ANY, refused, []() {});
        }
    });
    wm.setAPStaticIPConfig(kPortalApIp, kPortalGateway, kPortalSubnet);
    wm.setCaptivePortalEnable(true);
    wm.setClass(Str::Wifi::WM_CLASS);
    wm.setConfigPortalBlocking(false);
    wm.setConfigPortalTimeout(400);
    wm.setSaveConfigCallback(
        []() { lkbx->sm->process_event(WifiConnected{}); });
}

namespace {
// Entries into a Wi-Fi setup page, counted by the state machine. The portal
// belongs to one entry: a page that is left and entered again gets a portal
// of its own, after the one before it has closed.
std::atomic<uint32_t> portalEntries{0};
// The portal task exists. Set by the activity log task before it creates the
// task, cleared by the task as it ends.
std::atomic<bool> portalTaskAlive{false};
// The entry the activity log task last acted on. Only that task touches it.
uint32_t portalEntryHandled = 0;

// The portal task is created right after the MQTT client has stopped. Its
// stack is freed by the idle task a moment later, so the first attempts can
// fail where a later one fits.
constexpr int kPortalTaskAttempts = 8;
constexpr uint32_t kPortalTaskWaitMs = 250;

bool onWifiSetupPage() {
    return lkbx->sm->is("init.preflight.wifiSetup"_s) ||
           lkbx->sm->is("pairing.wifi"_s) || lkbx->sm->is("lockMenu.wifi"_s) ||
           lkbx->sm->is("emergency.wifi"_s) || lkbx->sm->is("wmConfig"_s) ||
           lkbx->sm->is("update.wifi"_s);
}

// Runs the portal for one entry into a Wi-Fi setup page. The MQTT client was
// stopped before this task was created (serviceWifiPortal).
void processWifiTask(void *pvParameters) {
    const uint32_t entry =
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pvParameters));
    // The page is still up, and it has not been left and entered again.
    auto pageStillOpen = [entry]() {
        return onWifiSetupPage() && portalEntries.load() == entry;
    };
    // Stopping the client can take seconds, and the page may have been left
    // meanwhile: then there is nothing to open.
    if (pageStillOpen()) {
        // The setup network is intentionally open on every track; the portal
        // guard still admits only clients associated with this access point.
        wm.startConfigPortal(Str::Wifi::PORTAL_SSID);
        registerCaptivePortalFallbackRoutes();
        wm.process();
    }

    // WiFiManager closes the portal by itself after its timeout while the
    // page can stay on the screen. Nothing needs the memory then, and a box
    // left on this page must not stay off the broker.
    bool mqttResumed = false;
    while (pageStillOpen()) {
        wm.process();
        if (!mqttResumed && !wm.getConfigPortalActive()) {
            resumeMqttAfterPortal();
            mqttResumed = true;
        }
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
    if (!mqttResumed) resumeMqttAfterPortal();
    TaskHandles::wifi = nullptr;
    portalTaskAlive.store(false);
    vTaskDelete(nullptr);
}
}  // namespace

void requestWifiPortal() { portalEntries.fetch_add(1); }

void serviceWifiPortal() {
    const uint32_t entry = portalEntries.load();
    if (entry == portalEntryHandled) return;
    // The portal of the entry before is still closing: its task has seen the
    // newer entry and ends. One portal at a time.
    if (portalTaskAlive.load()) return;
    portalEntryHandled = entry;
    // The page was left before the portal could open.
    if (!onWifiSetupPage()) return;

    // The portal and the MQTT client do not fit in internal RAM together on
    // R8 hardware (utils/mqttPause.h). Stop the client first, which gives
    // back its task stack and TLS session, and only then create the portal's
    // task: created first, that task had to fit beside the client, and on R8
    // it once did not (largest free block 2,560 bytes). This waits for the
    // MQTT task to end: about a second when connected, longer while the
    // client is in the middle of a connection attempt.
    pauseMqttForPortal();
    portalTaskAlive.store(true);
    if (startTaskWithRetry(processWifiTask, Tasks::wifi,
                           reinterpret_cast<void *>(
                               static_cast<uintptr_t>(entry)),
                           &TaskHandles::wifi, kPortalTaskAttempts,
                           kPortalTaskWaitMs)) {
        ESP_LOGI(WIFI_SETUP_TAG,
                 "Wi-Fi setup opening: at least %u bytes of the activity log "
                 "task's stack have stayed free",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
        return;
    }

    // No setup network. The client must not stay stopped for a portal that
    // never opened, and the page must not show a QR code for a network that
    // does not exist.
    portalTaskAlive.store(false);
    TaskHandles::wifi = nullptr;
    resumeMqttAfterPortal();
    ESP_LOGE(WIFI_SETUP_TAG, "Could not open Wi-Fi setup; starting MQTT again");
    if (portalEntries.load() == entry && onWifiSetupPage()) {
        lkbx->drawWifiSetupUnavailable();
    }
}
