#include "http_worker.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "FirmwareProvenance.h"
#include "constants/dashboard.h"
#include "constants/LogTags.h"
#include "tasks/TaskParams.h"
#include "services/serverOnline.h"

#if defined(VERSIONDEV)
#if !defined(LKBX_DEV_CA_PEM) || !defined(LKBX_DEV_API_TOKEN) || !defined(LKBX_DEV_API_BASE_URL)
#error Development HTTP requires the generated local fixture configuration
#endif
#endif

namespace {
constexpr int QUEUE_DEPTH = 4;
constexpr int MAX_REQUEST_MS = 60000;
constexpr int IO_TIMEOUT_MS = 1000;
constexpr int STOP_TIMEOUT_MS = 3000;
constexpr size_t MAX_RESPONSE_BYTES = 32768;
constexpr size_t MAX_BODY_BYTES = 32768;
constexpr size_t MAX_URL_BYTES = 2048;

struct HttpRequest {
    String url;
    String body;
    String deviceSecret;
    bool isPost = false;
    int retries = 0;
    int64_t deadlineUs = 0;
    int responseCode = -1;
    String responsePayload;
    bool dateSeen = false;
    uint64_t serverTimeMs = 0;
    int64_t dateReceivedUs = 0;
    bool serverOperation = false;
    bool clockProbe = false;
    bool attempted = false;
    uint32_t connectivityAttempt = 0;
    SemaphoreHandle_t completed = xSemaphoreCreateBinary();
    // The caller and queued/active worker each own a reference. Neither stores
    // the caller's stack address or task handle.
    std::atomic<unsigned> references{1};
    std::atomic<bool> cancelled{false};

    ~HttpRequest() {
        if (completed) vSemaphoreDelete(completed);
    }
    void release() {
        if (references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete this;
    }
};

std::mutex lifecycleMutex;
// The bounded queue stays allocated across restarts, so neither a concurrent
// submitter nor a stopping worker can observe a deleted queue.
QueueHandle_t httpQueue = nullptr;
std::atomic<bool> workerRunning{false};
std::atomic<bool> stopRequested{true};

bool validHttpsUrl(const std::string &url) {
    if (url.size() > MAX_URL_BYTES || url.compare(0, 8, "https://") != 0)
        return false;
    for (unsigned char c : url) {
        if (c <= 0x20 || c >= 0x7f || c == '\\' || c == '#') return false;
    }
    const auto end = url.find_first_of("/?", 8);
    const std::string authority = url.substr(8, end == std::string::npos
                                                  ? end : end - 8);
    // Reject userinfo, encoded/ambiguous authority and empty hosts. TLS still
    // performs certificate-chain and hostname verification for the target.
    if (authority.empty() || authority.front() == ':' ||
        authority.find_first_of("@%") != std::string::npos) return false;
    return true;
}

bool allowedUrl(const String &value) {
    const std::string url(value.c_str(), value.length());
    if (!validHttpsUrl(url)) return false;
#if defined(VERSIONDEV)
    const std::string base(LKBX_DEV_API_BASE_URL);
    // Generated fixture configuration supplies one HTTPS origin, without a
    // path, query or trailing slash. Never send its bearer to another origin.
    if (!validHttpsUrl(base) || base.find_first_of("/?", 8) != std::string::npos ||
        LKBX_DEV_CA_PEM[0] == '\0' || LKBX_DEV_API_TOKEN[0] == '\0') return false;
    if (url.compare(0, base.size(), base) != 0) return false;
    if (url.size() > base.size() && url[base.size()] != '/' &&
        url[base.size()] != '?') return false;
#endif
    return true;
}

bool configuredServerUrl(const String &value) {
    const std::string url(value.c_str(), value.length());
    const std::string base(RAD_SERVER);
    return validHttpsUrl(base) && base.find_first_of("/?", 8) == std::string::npos &&
           url.compare(0, base.size(), base) == 0 &&
           (url.size() == base.size() || url[base.size()] == '/' ||
            url[base.size()] == '?');
}

bool expired(const HttpRequest &req) {
    return req.cancelled.load(std::memory_order_acquire) ||
           stopRequested.load(std::memory_order_acquire) ||
           esp_timer_get_time() >= req.deadlineUs;
}

int remainingMs(const HttpRequest &req) {
    const int64_t remainingUs = req.deadlineUs - esp_timer_get_time();
    return remainingUs <= 0 ? 0 : static_cast<int>((remainingUs + 999) / 1000);
}

bool prepareIo(esp_http_client_handle_t client, const HttpRequest &req) {
    if (expired(req)) return false;
    return esp_http_client_set_timeout_ms(
               client, std::min(IO_TIMEOUT_MS, remainingMs(req))) == ESP_OK;
}

// The dashboard can take longer than one I/O slice to start its response, for
// example when the request crosses continents. Wait for the headers in bounded
// slices until the request deadline, so stop and cancellation are still seen.
// IDF reports a timed-out slice like any other failure (and resumes parsing on
// the next call), so a failure that returns before half its slice is final.
int64_t fetchHeaders(esp_http_client_handle_t client, const HttpRequest &req) {
    while (prepareIo(client, req)) {
        const int sliceMs = std::min(IO_TIMEOUT_MS, remainingMs(req));
        const int64_t startedUs = esp_timer_get_time();
        const int64_t contentLength = esp_http_client_fetch_headers(client);
        if (contentLength >= 0) return contentLength;
        if (esp_timer_get_time() - startedUs < static_cast<int64_t>(sliceMs) * 500)
            return -1;
    }
    return -1;
}

bool leapYear(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

// Accept only the standard HTTP IMF-fixdate form, not locale-dependent dates,
// obsolete formats, normalized invalid days, leap seconds or arbitrary epochs.
uint64_t parseServerDate(const char *value) {
    if (!value || std::strlen(value) != 29) return 0;
    const std::string date(value, 29);
    if (date[3] != ',' || date[4] != ' ' ||
        date[7] != ' ' || date[11] != ' ' || date[16] != ' ' ||
        date[19] != ':' || date[22] != ':' || date.compare(25, 4, " GMT") != 0)
        return 0;
    auto number = [&date](size_t offset, size_t length) {
        int result = 0;
        for (size_t i = offset; i < offset + length; ++i) {
            if (date[i] < '0' || date[i] > '9') return -1;
            result = result * 10 + date[i] - '0';
        }
        return result;
    };
    const int day = number(5, 2), year = number(12, 4);
    const int hour = number(17, 2), minute = number(20, 2), second = number(23, 2);
    const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int month = -1;
    for (int i = 0; i < 12; ++i) if (date.compare(8, 3, months[i]) == 0) month = i;
    if (year < 2024 || year > 2099 || month < 0 || day < 1 ||
        day > monthDays[month] + (month == 1 && leapYear(year) ? 1 : 0) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return 0;
    uint64_t days = 0;
    for (int y = 1970; y < year; ++y) days += leapYear(y) ? 366 : 365;
    for (int m = 0; m < month; ++m) days += monthDays[m] + (m == 1 && leapYear(year) ? 1 : 0);
    days += day - 1;
    const char *weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    if (date.compare(0, 3, weekdays[(days + 4) % 7]) != 0) return 0;
    return ((days * 24 + hour) * 3600 + minute * 60 + second) * 1000;
}

esp_err_t onHttpEvent(esp_http_client_event_t *event) {
    if (event->event_id != HTTP_EVENT_ON_HEADER || !event->user_data ||
        !event->header_key) return ESP_OK;
    const char *key = event->header_key;
    if (std::strlen(key) != 4 || (key[0] != 'D' && key[0] != 'd') ||
        (key[1] != 'A' && key[1] != 'a') || (key[2] != 'T' && key[2] != 't') ||
        (key[3] != 'E' && key[3] != 'e')) return ESP_OK;
    auto &req = *static_cast<HttpRequest *>(event->user_data);
    // Ambiguous duplicate Date headers never become trusted time.
    req.serverTimeMs = req.dateSeen ? 0 : parseServerDate(event->header_value);
    req.dateSeen = true;
    req.dateReceivedUs = esp_timer_get_time();
    return ESP_OK;
}

bool addHeaders(esp_http_client_handle_t client, const HttpRequest &req) {
    auto add = [client](const char *name, const char *value) {
        return esp_http_client_set_header(client, name, value) == ESP_OK;
    };
    if (!add("Accept-Encoding", "identity") ||
        !add("X-RAD-Firmware-Provenance-Capability", "1") ||
        (req.isPost && !add("Content-Type", "application/json")) ||
        (req.deviceSecret.length() > 0 &&
         !add("x-lkbx-device-secret", req.deviceSecret.c_str()))) return false;
#if defined(VERSIONDEV)
    const std::string authorization = std::string("Bearer ") + LKBX_DEV_API_TOKEN;
    if (!add("Authorization", authorization.c_str())) return false;
#endif
    const auto token = firmware::provenance::currentToken();
    return token.empty() ||
           (add("X-RAD-Firmware-Provenance", token.c_str()) &&
            add("X-RAD-Firmware-Provenance-ID",
                firmware::provenance::currentTokenId().c_str()) &&
            add("X-RAD-Firmware-Image-SHA256",
                firmware::provenance::currentImageSha256().c_str()));
}

int executeAttempt(HttpRequest &req) {
    esp_http_client_config_t config = {};
    config.url = req.url.c_str();
    config.method = req.isPost ? HTTP_METHOD_POST : HTTP_METHOD_GET;
    config.transport_type = HTTP_TRANSPORT_OVER_SSL;
    config.timeout_ms = std::min(IO_TIMEOUT_MS, remainingMs(req));
    config.buffer_size = 1024;
    config.buffer_size_tx = 2048;
    config.disable_auto_redirect = true;
    config.skip_cert_common_name_check = false;
    config.event_handler = onHttpEvent;
    config.user_data = &req;
#if defined(VERSIONDEV)
    config.cert_pem = LKBX_DEV_CA_PEM;
#elif defined(FIRMWARE_USE_IDF_CRT_BUNDLE)
    config.crt_bundle_attach = esp_crt_bundle_attach;
#else
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;
#endif
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) return -1;
    struct ClientCleanup {
        esp_http_client_handle_t client;
        ~ClientCleanup() {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
        }
    } cleanup{client};

    if (!addHeaders(client, req) || !prepareIo(client, req) ||
        esp_http_client_open(client, req.isPost ? req.body.length() : 0) != ESP_OK)
        return -1;
    size_t written = 0;
    while (req.isPost && written < req.body.length()) {
        if (!prepareIo(client, req)) return -1;
        const int count = esp_http_client_write(
            client, req.body.c_str() + written, req.body.length() - written);
        if (count <= 0) return -1;
        written += static_cast<size_t>(count);
    }
    const int64_t contentLength = fetchHeaders(client, req);
    const int status = esp_http_client_get_status_code(client);
    if (expired(req) || contentLength < 0 || status < 100 || status > 599 ||
        contentLength > static_cast<int64_t>(MAX_RESPONSE_BYTES)) return -1;
    // The explicit open/read API never follows redirects. Also reject the
    // redirect response immediately, without consuming an untrusted body.
    if (status >= 300 && status < 400) return status;

    String response;
    char buffer[512];
    // fetch_headers may already have parsed the complete body into IDF's cache.
    // Always drain read(), even when is_complete_data_received is already true.
    while (true) {
        if (!prepareIo(client, req)) return -1;
        const int count = esp_http_client_read(client, buffer, sizeof(buffer));
        if (count < 0 || expired(req)) return -1;
        if (count == 0) {
            if (!esp_http_client_is_complete_data_received(client)) return -1;
            break;
        }
        if (response.length() + static_cast<size_t>(count) > MAX_RESPONSE_BYTES ||
            !response.concat(buffer, count)) return -1;
    }
    req.responsePayload = std::move(response);
    return status;
}

void completeRequest(HttpRequest *req) {
    if (expired(*req)) {
        req->responseCode = -1;
        req->responsePayload.clear();
    }
    if (req->serverOperation && req->attempted) {
        const bool success = req->responseCode >= 200 && req->responseCode < 300 &&
            (!req->clockProbe || (req->responseCode == 200 && req->serverTimeMs != 0));
        completeServerHttpAttempt(req->connectivityAttempt, success);
    }
    xSemaphoreGive(req->completed);
    req->release();
}

void httpWorkerTask(void *) {
    while (true) {
        HttpRequest *req = nullptr;
        if (xQueueReceive(httpQueue, &req, pdMS_TO_TICKS(50)) == pdTRUE) {
            for (int attempt = 0; attempt <= req->retries && !expired(*req); ++attempt) {
                req->responsePayload.clear();
                req->dateSeen = false;
                req->serverTimeMs = 0;
                req->attempted = true;
                req->connectivityAttempt = beginServerHttpAttempt();
                req->responseCode = executeAttempt(*req);
                if (req->responseCode > 0 && req->responseCode < 500) break;
                if (attempt < req->retries) {
                    for (int delay = 0; delay < 500 && !expired(*req); delay += 50)
                        vTaskDelay(pdMS_TO_TICKS(50));
                }
            }
            if (req->responseCode < 0)
                ESP_LOGW(HTTP_TAG, "%s request failed without an HTTP response",
                         req->isPost ? "POST" : "GET");
            completeRequest(req);
        }
        if (stopRequested.load(std::memory_order_acquire)) {
            // Acceptance was disabled before stopRequested was set. Drain all
            // existing requests and wake their callers before reporting stopped.
            while (xQueueReceive(httpQueue, &req, 0) == pdTRUE) completeRequest(req);
            {
                std::lock_guard<std::mutex> lock(lifecycleMutex);
                TaskHandles::httpWorker = nullptr;
                workerRunning.store(false, std::memory_order_release);
            }
            vTaskDelete(nullptr);
            return;
        }
    }
}

int enqueue(const String &url, const String &body, bool isPost,
            String *responsePayload, int timeoutMs, int retries,
            uint64_t *serverTimeMs = nullptr, const String *deviceSecret = nullptr) {
    if (responsePayload) responsePayload->clear();
    if (serverTimeMs) *serverTimeMs = 0;
    if (!allowedUrl(url) || body.length() > MAX_BODY_BYTES || timeoutMs <= 0 ||
        retries < 0 || retries > 2 ||
        (deviceSecret && !configuredServerUrl(url))) return -1;
    HttpRequest *req = new (std::nothrow) HttpRequest;
    if (!req) return -1;
    if (!req->completed) {
        req->release();
        return -1;
    }
    req->url = url;
    req->body = body;
    if (deviceSecret) req->deviceSecret = *deviceSecret;
    if (req->url.length() != url.length() || req->body.length() != body.length() ||
        (deviceSecret && req->deviceSecret.length() != deviceSecret->length())) {
        req->release();
        return -1;
    }
    req->isPost = isPost;
    req->serverOperation = configuredServerUrl(url);
    req->clockProbe = serverTimeMs != nullptr;
    req->retries = retries;
    req->deadlineUs = esp_timer_get_time() +
                      static_cast<int64_t>(std::min(timeoutMs, MAX_REQUEST_MS)) * 1000;
    bool queued = false;
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        if (workerRunning.load(std::memory_order_acquire) &&
            !stopRequested.load(std::memory_order_acquire)) {
            req->references.fetch_add(1, std::memory_order_relaxed);
            queued = xQueueSend(httpQueue, &req, 0) == pdTRUE;
            if (!queued) req->release();
        }
    }
    int code = -1;
    if (queued) {
        const int waitMs = remainingMs(*req);
        const TickType_t waitTicks = waitMs > 0
            ? std::max<TickType_t>(1, pdMS_TO_TICKS(waitMs)) : 0;
        if (xSemaphoreTake(req->completed, waitTicks) == pdTRUE) {
            code = req->responseCode;
            if (responsePayload) *responsePayload = std::move(req->responsePayload);
            if (serverTimeMs && code == 200 && req->serverTimeMs && !expired(*req))
                *serverTimeMs = req->serverTimeMs +
                    static_cast<uint64_t>(esp_timer_get_time() - req->dateReceivedUs) / 1000;
        } else {
            // The worker keeps its reference and owns all later result writes.
            req->cancelled.store(true, std::memory_order_release);
            // An abandoned local operation is a failure now. Invalidate its
            // generation so a late transport result cannot report success.
            if (req->serverOperation) clearServerOnline();
        }
    }
    req->release();
    return code;
}
}  // namespace

void initHttpWorker() {
    std::lock_guard<std::mutex> lock(lifecycleMutex);
    if (workerRunning.load(std::memory_order_acquire)) return;
    if (!httpQueue) httpQueue = xQueueCreate(QUEUE_DEPTH, sizeof(HttpRequest *));
    if (!httpQueue) return;
    stopRequested.store(false, std::memory_order_release);
    workerRunning.store(true, std::memory_order_release);
    if (xTaskCreatePinnedToCore(httpWorkerTask, Tasks::httpWorker.name,
                               Tasks::httpWorker.stackSize, nullptr,
                               Tasks::httpWorker.priority,
                               &TaskHandles::httpWorker, Tasks::httpWorker.core) != pdPASS) {
        TaskHandles::httpWorker = nullptr;
        stopRequested.store(true, std::memory_order_release);
        workerRunning.store(false, std::memory_order_release);
    }
}

bool stopHttpWorker() {
    {
        std::lock_guard<std::mutex> lock(lifecycleMutex);
        stopRequested.store(true, std::memory_order_release);
    }
    const int64_t deadlineUs = esp_timer_get_time() + STOP_TIMEOUT_MS * 1000;
    while (workerRunning.load(std::memory_order_acquire)) {
        if (esp_timer_get_time() >= deadlineUs) {
            ESP_LOGW(HTTP_TAG, "HTTP worker is still releasing its active request");
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}

bool getAuthenticatedServerTimeMs(uint64_t *out) {
    if (!out) return false;
    return enqueue(String(RAD_SERVER "/api/health-check"), String(), false, nullptr,
                   5000, 0, out) == 200 && *out != 0;
}

int get(const String &url, String *responsePayload, int timeoutMs, int retries) {
    return enqueue(url, String(), false, responsePayload, timeoutMs, retries);
}

int getWithDeviceSecret(const String &url, const String &deviceSecret,
                        String *responsePayload, int timeoutMs, int retries) {
    return enqueue(url, String(), false, responsePayload, timeoutMs, retries, nullptr,
                   deviceSecret.length() > 0 ? &deviceSecret : nullptr);
}

int post(const String &url, const String &body, String *responsePayload,
         int timeoutMs, int retries) {
    return enqueue(url, body, true, responsePayload, timeoutMs, retries);
}

int post(const String &url, const JsonDocument &doc, String *responsePayload,
         int timeoutMs, int retries) {
    const size_t expectedLength = measureJson(doc);
    if (expectedLength > MAX_BODY_BYTES) {
        if (responsePayload) responsePayload->clear();
        return -1;
    }
    String body;
    serializeJson(doc, body);
    if (body.length() != expectedLength) {
        if (responsePayload) responsePayload->clear();
        return -1;
    }
    return post(url, body, responsePayload, timeoutMs, retries);
}
