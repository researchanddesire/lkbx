#include "wm.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_netif_sta_list.h>

#include <atomic>

#include "PortalPeer.h"
#include "PortalRoutes.h"
#include "portal_guard.h"

#include "constants/LogTags.h"
#include "constants/dashboard.h"
#include "constants/strings.h"
#include "machine.h"
#include "motor/Motor.h"
#include "services/lastInteraction.h"
#include "services/memoryWatch.h"
#include "services/mqtt.h"
#include "services/serverOnline.h"
#include "tasks/TaskStart.h"
#include "utils/networkRecovery.h"
#include "utils/stationKeeper.h"

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
    // Failed internal allocations are noted from here on: network recovery
    // looks into a lost broker sooner after one (utils/networkRecovery.h).
    initMemoryWatch();
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

// The portal task is created right after the MQTT client has closed its
// connection, which returns its TLS session to the heap over a moment, so
// the first attempts can fail where a later one fits. If it still does not
// fit beside the client's task, that task is stopped after all, and its stack
// is freed by the idle task a moment later.
constexpr int kPortalTaskAttemptsBesideClient = 4;
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
    // The most this task ever needed of its stack, for the rig: Tasks::wifi
    // is sized from this line, and R8 often has no free block much larger
    // than that stack.
    ESP_LOGI(WIFI_SETUP_TAG,
             "Wi-Fi setup closed: at least %u bytes of its %u-byte stack "
             "have stayed free",
             static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
             static_cast<unsigned>(Tasks::wifi.stackSize));
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

    // The portal and a connected MQTT client do not fit in internal RAM
    // together on R8 hardware (utils/mqttPause.h). Take the client off the
    // broker first, which gives back its TLS session, and only then create
    // the portal's task: created first, that task had to fit beside the
    // connected client, and on R8 it once did not (largest free block 2,560
    // bytes). This waits for the MQTT task: about a second when connected,
    // longer while the client is in the middle of a connection attempt.
    pauseMqttForPortal();
    portalTaskAlive.store(true);
    void *argument =
        reinterpret_cast<void *>(static_cast<uintptr_t>(entry));
    bool opened = startTaskWithRetry(processWifiTask, Tasks::wifi, argument,
                                     &TaskHandles::wifi,
                                     kPortalTaskAttemptsBesideClient,
                                     kPortalTaskWaitMs);
    if (!opened) {
        // No block for the portal's stack. The client's task is kept so that
        // it never has to be created again, but its 10 KB stack is what the
        // portal's own one fits into, so it is given up now. The client
        // then needs a new task after the portal, and if that cannot be
        // created for two minutes the box restarts at rest
        // (utils/networkRecovery.h).
        if (releaseMqttTaskForPortal()) {
            ESP_LOGW(WIFI_SETUP_TAG,
                     "Wi-Fi setup did not fit beside the MQTT task; that "
                     "task was stopped for it");
        }
        opened = startTaskWithRetry(processWifiTask, Tasks::wifi, argument,
                                    &TaskHandles::wifi, kPortalTaskAttempts,
                                    kPortalTaskWaitMs);
    }
    if (opened) {
        // This measures the task that opens the portal, not the portal's own
        // task (that one reports its stack when it closes, above), so the
        // line names the task it measured.
        ESP_LOGI(WIFI_SETUP_TAG,
                 "Wi-Fi setup opening: at least %u bytes of %s's stack have "
                 "stayed free",
                 static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),
                 pcTaskGetName(nullptr));
        // The portal now runs beside the client's task, so what is left for
        // it goes to the log for the rig.
        ESP_LOGI(WIFI_SETUP_TAG,
                 "Wi-Fi setup opening: largest free internal block %u bytes, "
                 "%u bytes free",
                 static_cast<unsigned>(
                     heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                 static_cast<unsigned>(
                     heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
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

void serviceWifiStation() {
    // Only the activity log task comes here.
    static StationKeeper keeper;
    const bool connected = WiFi.status() == WL_CONNECTED;
    // Looked at only while the station is down: a portal that is open, about
    // to be opened, or still closing owns the Wi-Fi mode.
    const bool held =
        !connected &&
        (portalTaskAlive.load() || portalEntries.load() != portalEntryHandled ||
         onWifiSetupPage());
    if (!keeper.due(connected, held, millis())) return;
    // Nothing to connect to before the box has been given a network.
    if (!wm.getWiFiIsSaved()) return;
    ESP_LOGW(WIFI_CONNECT_TAG,
             "Wi-Fi station has been down for %u s; connecting again",
             static_cast<unsigned>(stationKeeper::kDownBeforeBeginMs / 1000));
    WiFi.begin();
}

namespace {
// Only the activity log task touches these.
NetworkRecovery networkRecoveryPlan;
// The question to the dashboard, until its answer has been taken.
HttpPending *networkCheck = nullptr;
uint32_t networkRecoveryLookedMs = 0;

// Every step is logged with the memory picture: a box that was cut off on the
// rig had internal RAM in pieces the whole time.
void logNetworkRecovery(const char *step) {
    ESP_LOGW(WIFI_CONNECT_TAG,
             "Network recovery: %s (largest free internal block %u bytes, "
             "%u bytes free, lowest ever %u bytes, %u internal allocations "
             "failed since boot)",
             step,
             static_cast<unsigned>(
                 heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(
                 heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(internalAllocationFailures()));
}

// Whether the box may restart now to get its network back. A restart goes
// through the boot sequence, which restores a lock, its session and its
// deadline from flash, as it does every time the box wakes from deep sleep.
// The box still only restarts when nothing is going on.
bool atRestForRestart() {
    // A button, the dial, a cable or a message within the last 30 seconds.
    if (idleState == IdleState::NOT_IDLE) return false;
    // The motor is being driven.
    if (TaskHandles::currentWatch != nullptr) return false;
    // A setup portal is open, closing or about to open.
    if (portalTaskAlive.load() || portalEntries.load() != portalEntryHandled) {
        return false;
    }
    // Only from a page the boot sequence leads back to by itself: the home
    // menu or a locked page. Never while the box is locking or unlocking,
    // asking Chaster, on a break, in a menu, on a setup or pairing page,
    // updating or still starting. And not during an emergency lockout: the
    // wearer has just used the safety release. A boot no longer locks the
    // session an emergency unlock ended (utils/emergencyRecord.h), but that
    // has to be seen on the rig before this page joins the list.
    const bool restingPage =
        lkbx->sm->is("home.idle"_s) || lkbx->sm->is("locked"_s) ||
        lkbx->sm->is("locked.timer"_s) || lkbx->sm->is("locked.ready"_s) ||
        lkbx->sm->is("locked.frozen"_s);
    if (!restingPage) return false;
    // A local session is the box's own and needs no network, so the box
    // does not restart by itself while it holds one. A boot now keeps a local
    // session (bootKeepsLocalSession); this stays until that has been seen
    // on the rig. A value that cannot be read counts as a local session.
    bool localSession = true;
    if (xSemaphoreTake(preferencesMutex, portTICK_PERIOD_MS * 1000) == pdTRUE) {
        preferences.begin(Keys::base);
        localSession = preferences.getBool(Keys::Base::isLocalPlay, false);
        preferences.end();
        xSemaphoreGive(preferencesMutex);
    }
    return !localSession;
}
}  // namespace

void serviceNetworkRecovery() {
    // The dashboard was asked: its answer first.
    if (networkCheck != nullptr) {
        int status = -1;
        if (!takeHttpResult(networkCheck, status, nullptr)) return;
        // Any HTTP response at all means the network carries traffic.
        const bool answered = status > 0;
        networkRecoveryPlan.checked(answered, millis());
        if (answered && mqttClientCannotStart()) {
            // Not the broker's fault either: the box's own client is what is
            // missing, and that has its own clock (CLIENT_DOWN below).
            ESP_LOGW(WIFI_CONNECT_TAG,
                     "Network recovery: the dashboard answers (HTTP %d), but "
                     "the box's own MQTT client cannot be started",
                     status);
        } else if (answered) {
            ESP_LOGW(WIFI_CONNECT_TAG,
                     "Network recovery: the dashboard answers (HTTP %d), so "
                     "only the broker is out of reach; nothing to do",
                     status);
        } else if (networkRecoveryPlan.restartWanted()) {
            logNetworkRecovery(
                "step 2, the dashboard still does not answer; restarting "
                "once the box is at rest");
        } else {
            logNetworkRecovery("the dashboard does not answer either");
        }
        return;
    }

    // Once a second is plenty.
    const uint32_t now = millis();
    if (static_cast<uint32_t>(now - networkRecoveryLookedMs) < 1000) return;
    networkRecoveryLookedMs = now;

    RecoveryInputs in;
    in.brokerUp = mqttConnected;
    in.stationUp = WiFi.status() == WL_CONNECTED;
    in.brokerExpected = mqttClientExpected();
    in.clientDown = mqttClientCannotStart();
    in.memoryShort =
        internalMemoryWasShort(networkRecovery::kShortageRecentMs);
    // Looked at only while the box is off the broker: a setup page owns the
    // Wi-Fi mode, as for the station keeper.
    in.held = !in.brokerUp &&
              (portalTaskAlive.load() ||
               portalEntries.load() != portalEntryHandled || onWifiSetupPage());
    in.atRest = networkRecoveryPlan.restartWanted() && atRestForRestart();

    const bool wasStepped = networkRecoveryPlan.stepped();
    const RecoveryStep step = networkRecoveryPlan.next(in, now);
    if (wasStepped && in.brokerUp) {
        logNetworkRecovery("back on the broker");
    }

    switch (step) {
        case RecoveryStep::CHECK:
            logNetworkRecovery(
                networkRecoveryPlan.afterShortage()
                    ? "off the broker with Wi-Fi up after internal RAM ran "
                      "short; asking the dashboard"
                    : "off the broker with Wi-Fi up; asking the dashboard");
            networkCheck =
                getDetached(String(RAD_SERVER) + Str::Api::HEALTH_CHECK,
                            networkRecovery::kCheckTimeoutMs);
            // A question that cannot even be handed over is no answer.
            if (networkCheck == nullptr) {
                networkRecoveryPlan.checked(false, now);
            }
            break;
        case RecoveryStep::RECONNECT: {
            logNetworkRecovery(
                "step 1, cut off; stopping MQTT and connecting Wi-Fi again");
            // Gives back the client's task stack and its TLS session. The
            // activity log task starts it again two seconds later
            // (serviceMqttPause) and the state is read once it has connected.
            restartMqttForRecovery();
            // Drops the association and connects with the saved credentials.
            // If that fails the station stays down, and the station keeper
            // begins it again.
            const bool begun = WiFi.reconnect();
            ESP_LOGW(WIFI_CONNECT_TAG,
                     "Network recovery: Wi-Fi station %s",
                     begun ? "is connecting again"
                           : "could not be told to connect again");
            break;
        }
        case RecoveryStep::CLIENT_DOWN:
            logNetworkRecovery(
                "the MQTT client could not be started for two minutes; "
                "restarting once the box is at rest");
            break;
        case RecoveryStep::RESTART:
            logNetworkRecovery(
                networkRecoveryPlan.forClient()
                    ? "restarting now to start the MQTT client again"
                    : "step 2, restarting now to get the network back");
            Motor::clear();
            // Lets the line above reach the serial port.
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_restart();
            break;
        case RecoveryStep::NONE:
        default:
            break;
    }
}
