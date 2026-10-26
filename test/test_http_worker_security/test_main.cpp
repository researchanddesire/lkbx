#include "services/http_worker.h"
#include <esp_http_client.h>
#include <esp_timer.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
struct MockSemaphore {
    std::mutex mutex;
    std::condition_variable condition;
    bool signalled = false;
};
struct MockQueue {
    std::mutex mutex;
    std::condition_variable condition;
    size_t capacity;
    std::deque<void *> entries;
};
std::atomic<int> liveSemaphores{0}, liveClients{0};
std::vector<std::thread> workerThreads;
std::vector<std::unique_ptr<MockQueue>> queues;

SemaphoreHandle_t xSemaphoreCreateBinary() { ++liveSemaphores; return new MockSemaphore; }
void vSemaphoreDelete(SemaphoreHandle_t sem) { delete sem; --liveSemaphores; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks) {
    std::unique_lock<std::mutex> lock(sem->mutex);
    if (!sem->condition.wait_for(lock, std::chrono::milliseconds(ticks),
                                 [&] { return sem->signalled; })) return pdFALSE;
    sem->signalled = false;
    return pdTRUE;
}
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem) {
    std::lock_guard<std::mutex> lock(sem->mutex);
    sem->signalled = true;
    sem->condition.notify_one();
    return pdTRUE;
}
QueueHandle_t xQueueCreate(unsigned capacity, size_t elementSize) {
    assert(elementSize == sizeof(void *));
    auto q = std::make_unique<MockQueue>();
    q->capacity = capacity;
    auto ptr = q.get();
    queues.push_back(std::move(q));
    return ptr;
}
BaseType_t xQueueSend(QueueHandle_t q, const void *value, TickType_t ticks) {
    assert(ticks == 0); // Saturation must fail promptly, not strand a caller.
    std::lock_guard<std::mutex> lock(q->mutex);
    if (q->entries.size() == q->capacity) return pdFALSE;
    void *entry;
    std::memcpy(&entry, value, sizeof(entry));
    q->entries.push_back(entry);
    q->condition.notify_one();
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t q, void *value, TickType_t ticks) {
    std::unique_lock<std::mutex> lock(q->mutex);
    if (!q->condition.wait_for(lock, std::chrono::milliseconds(ticks),
                               [&] { return !q->entries.empty(); })) return pdFALSE;
    std::memcpy(value, &q->entries.front(), sizeof(void *));
    q->entries.pop_front();
    return pdTRUE;
}
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *, uint32_t,
                                 void *arg, UBaseType_t, TaskHandle_t *handle, BaseType_t) {
    *handle = reinterpret_cast<void *>(1);
    workerThreads.emplace_back([=] { entry(arg); });
    return pdPASS;
}
void vTaskDelay(TickType_t ticks) { std::this_thread::sleep_for(std::chrono::milliseconds(ticks)); }
void vTaskDelete(TaskHandle_t task) { assert(task == nullptr); }
int64_t esp_timer_get_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
int esp_crt_bundle_attach(void *) { return ESP_OK; }
int arduino_esp_crt_bundle_attach(void *) { return ESP_OK; }

struct Gate {
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false, released = false;
    void block() {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        condition.notify_all();
        condition.wait(lock, [&] { return released; });
    }
    void awaitEntry() {
        std::unique_lock<std::mutex> lock(mutex);
        assert(condition.wait_for(lock, 2s, [&] { return entered; }));
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex);
        released = true;
        condition.notify_all();
    }
};
struct Scenario {
    std::string body = "ok";
    int status = 200;
    int64_t declaredLength = -2;
    bool chainValid = true, hostnameValid = true, truncated = false, cachedAtHeaders = false;
    int readDelayMs = 0, chunkSize = 512;
    std::shared_ptr<Gate> openGate;
    std::vector<std::pair<std::string, std::string>> headers;
};
struct Record {
    std::string url, certificate, writtenBody;
    std::map<std::string, std::string> headers;
    bool bundle = false, skipHostname = false, redirectsDisabled = false;
    esp_http_client_transport_t transport = HTTP_TRANSPORT_UNKNOWN;
    esp_http_client_method_t method = HTTP_METHOD_GET;
};
struct MockHttp {
    Scenario scenario;
    Record record;
    size_t offset = 0;
    int timeout = 0;
    esp_err_t (*eventHandler)(esp_http_client_event_t *) = nullptr;
    void *userData = nullptr;
};
std::mutex httpMutex;
std::deque<Scenario> scenarios;
std::vector<Record> records;

void schedule(Scenario s) { std::lock_guard<std::mutex> lock(httpMutex); scenarios.push_back(std::move(s)); }
std::vector<Record> snapshots() { std::lock_guard<std::mutex> lock(httpMutex); return records; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    std::lock_guard<std::mutex> lock(httpMutex);
    auto client = new MockHttp;
    ++liveClients;
    if (!scenarios.empty()) { client->scenario = std::move(scenarios.front()); scenarios.pop_front(); }
    client->record.url = config->url;
    client->record.certificate = config->cert_pem ? config->cert_pem : "";
    client->record.bundle = config->crt_bundle_attach != nullptr;
    client->record.skipHostname = config->skip_cert_common_name_check;
    client->record.redirectsDisabled = config->disable_auto_redirect;
    client->record.transport = config->transport_type;
    client->record.method = config->method;
    client->timeout = config->timeout_ms;
    client->eventHandler = config->event_handler;
    client->userData = config->user_data;
    return client;
}
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char *key, const char *value) {
    client->record.headers[key] = value;
    return ESP_OK;
}
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t client, int timeout) {
    assert(timeout > 0 && timeout <= 1000);
    client->timeout = timeout;
    return ESP_OK;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int) {
    assert(client->timeout > 0 && client->timeout <= 1000);
    if (client->scenario.openGate) client->scenario.openGate->block();
    const auto &r = client->record;
    if (r.transport != HTTP_TRANSPORT_OVER_SSL || r.skipHostname ||
        (!r.bundle && r.certificate.empty()) || !client->scenario.chainValid ||
        !client->scenario.hostnameValid) return ESP_FAIL;
    return ESP_OK;
}
int esp_http_client_write(esp_http_client_handle_t client, const char *data, int count) {
    // Force the real worker to handle partial writes.
    int written = std::min(count, 7);
    client->record.writtenBody.append(data, written);
    return written;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t client) {
    for (const auto &header : client->scenario.headers) {
        esp_http_client_event_t event{HTTP_EVENT_ON_HEADER, client->userData,
                                     header.first.c_str(), header.second.c_str()};
        if (client->eventHandler) client->eventHandler(&event);
    }
    return client->scenario.declaredLength == -2
        ? static_cast<int64_t>(client->scenario.body.size()) : client->scenario.declaredLength;
}
int esp_http_client_get_status_code(esp_http_client_handle_t client) { return client->scenario.status; }
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t client) {
    return !client->scenario.truncated &&
        (client->scenario.cachedAtHeaders || client->offset == client->scenario.body.size());
}
int esp_http_client_read(esp_http_client_handle_t client, char *buffer, int size) {
    if (client->scenario.readDelayMs) std::this_thread::sleep_for(std::chrono::milliseconds(client->scenario.readDelayMs));
    const int count = std::min({size, client->scenario.chunkSize,
        static_cast<int>(client->scenario.body.size() - client->offset)});
    std::memcpy(buffer, client->scenario.body.data() + client->offset, count);
    client->offset += count;
    return count;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t) { return ESP_OK; }
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    { std::lock_guard<std::mutex> lock(httpMutex); records.push_back(client->record); }
    delete client;
    --liveClients;
    return ESP_OK;
}

#ifdef VERSIONDEV
const std::string BASE = LKBX_DEV_API_BASE_URL;
#else
const std::string BASE = "https://dashboard.example.test";
#endif
String url(const std::string &path = "/api/test") { return String(BASE + path); }
void reset() {
    assert(stopHttpWorker());
    for (auto &thread : workerThreads) thread.join();
    workerThreads.clear();
    assert(liveClients == 0);
    assert(liveSemaphores == 0);
    std::lock_guard<std::mutex> lock(httpMutex);
    scenarios.clear();
    records.clear();
    initHttpWorker();
}
std::future<int> asyncGet(int timeout = 1000) {
    return std::async(std::launch::async, [timeout] {
        String response;
        int code = get(url(), &response, timeout);
        if (code == -1) assert(response.length() == 0);
        return code;
    });
}
void waitForQueue(size_t size) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        { std::lock_guard<std::mutex> lock(queues.front()->mutex);
          if (queues.front()->entries.size() == size) return; }
        std::this_thread::sleep_for(1ms);
    }
    assert(false && "queue did not reach expected depth");
}

void testTlsAndUrls() {
    reset();
    for (const char *bad : {"http://dashboard.example.test/api", "https://", "https://:443/api",
                           "https://user@dashboard.example.test/api", "https://host/#fragment",
                           "https://host/\r\nInjected:yes", "https://host\\@other/api",
                           "https://%68ost/api"}) {
        String response("stale");
        assert(get(String(bad), &response) == -1 && response.length() == 0);
    }
    assert(get(String(BASE + std::string(2050, '/'))) == -1);
    assert(get(String(BASE + std::string("/bad\0suffix", 11))) == -1);
#ifdef VERSIONDEV
    for (const auto &bad : {BASE + ".attacker.test/api", BASE + "0/api",
                           std::string("https://localhost:8443/api"),
                           std::string("https://127.0.0.1:8444/api")})
        assert(get(String(bad)) == -1);
#endif
    assert(snapshots().empty() && liveClients == 0);
    Scenario badChain; badChain.chainValid = false; schedule(badChain);
    assert(get(url()) == -1);
    Scenario wrongHost; wrongHost.hostnameValid = false; schedule(wrongHost);
    assert(get(url()) == -1);
    String response;
    assert(post(url(), String("{\"hello\":\"world\"}"), &response) == 200);
    assert(std::string(response.c_str()) == "ok");
    const auto r = snapshots();
    assert(r.size() == 3);
    for (const auto &record : r) {
        assert(record.transport == HTTP_TRANSPORT_OVER_SSL);
        assert(!record.skipHostname && record.redirectsDisabled);
        assert(record.headers.at("X-RAD-Firmware-Provenance-Capability") == "1");
        assert(record.headers.at("X-RAD-Firmware-Provenance") == "public-provenance-fixture");
        assert(record.headers.at("X-RAD-Firmware-Provenance-ID") == "public-token-id");
        assert(record.headers.at("X-RAD-Firmware-Image-SHA256") == "public-image-hash");
#ifdef VERSIONDEV
        assert(!record.bundle && record.certificate == LKBX_DEV_CA_PEM);
        assert(record.headers.at("Authorization") == std::string("Bearer ") + LKBX_DEV_API_TOKEN);
#else
        assert(record.bundle && record.certificate.empty());
        assert(record.headers.count("Authorization") == 0);
#endif
    }
    assert(r.back().method == HTTP_METHOD_POST);
    assert(r.back().writtenBody == "{\"hello\":\"world\"}");
    assert(r.back().headers.at("Content-Type") == "application/json");
    Scenario redirect; redirect.status = 302; redirect.body = "redirect"; schedule(redirect);
    assert(get(url(), &response, 1000, 2) == 302 && response.length() == 0);
    assert(snapshots().size() == 4); // no redirect hop, even with retries requested
}

void testBoundsAndRetry() {
    reset();
    String response("stale");
    assert(post(url(), String(std::string(32769, 'a')), &response) == -1);
    JsonDocument tooLarge{std::string(32769, 'a')};
    assert(post(url(), tooLarge, &response) == -1);
    assert(get(url(), &response, 0) == -1);
    assert(get(url(), &response, 1000, 3) == -1);
    assert(snapshots().empty());
    Scenario large; large.body.assign(32769, 'a'); schedule(large);
    assert(get(url(), &response) == -1 && response.length() == 0);
    large.declaredLength = 0; schedule(large); // streaming/chunked, no declared limit
    assert(get(url(), &response) == -1 && response.length() == 0);
    Scenario boundary; boundary.body.assign(32768, 'a'); boundary.declaredLength = 0; schedule(boundary);
    assert(get(url(), &response) == 200 && response.length() == 32768);
    Scenario cached; cached.cachedAtHeaders = true; cached.body = "already cached by fetch_headers"; schedule(cached);
    assert(get(url(), &response) == 200 && std::string(response.c_str()) == cached.body);
    Scenario truncated; truncated.truncated = true; schedule(truncated);
    assert(get(url(), &response) == -1 && response.length() == 0);
    Scenario slow; slow.body.assign(100, 'x'); slow.chunkSize = 1; slow.readDelayMs = 10; schedule(slow);
    auto started = std::chrono::steady_clock::now();
    assert(get(url(), &response, 30) == -1);
    assert(std::chrono::steady_clock::now() - started < 250ms);
    assert(stopHttpWorker());
    reset();
    Scenario fail; fail.status = 503; schedule(fail); schedule(Scenario{});
    assert(get(url(), &response, 1500, 1) == 200);
    assert(snapshots().size() == 2);
    for (const auto &r : snapshots()) assert(r.transport == HTTP_TRANSPORT_OVER_SSL);
    reset();
    schedule(fail); Scenario tlsFailure; tlsFailure.chainValid = false; schedule(tlsFailure);
    assert(get(url(), &response, 1500, 1) == -1 && response.length() == 0);
}

void testCallerTimeoutAndLateCompletion() {
    reset();
    for (int repetition = 0; repetition < 12; ++repetition) {
        auto gate = std::make_shared<Gate>();
        Scenario late; late.openGate = gate; schedule(late);
        auto caller = asyncGet(25);
        gate->awaitEntry();
        assert(caller.get() == -1); // caller stack and host thread are now gone
        auto nextCaller = asyncGet(1000);
        gate->release(); // transport completes after caller destruction
        assert(nextCaller.get() == 200); // late result cannot wake this request
    }
    assert(stopHttpWorker());
    assert(liveSemaphores == 0);
    reset();
    auto gate = std::make_shared<Gate>(); Scenario active; active.openGate = gate; schedule(active);
    auto caller = asyncGet(1000); gate->awaitEntry();
    auto queued = asyncGet(20);
    assert(queued.get() == -1); // timeout while waiting in queue
    gate->release();
    assert(caller.get() == 200);
    assert(stopHttpWorker());
    assert(snapshots().size() == 1); // expired queued request never opens a socket
    assert(liveSemaphores == 0);
}

void testStopAndQueueSaturation() {
    reset();
    auto gate = std::make_shared<Gate>(); Scenario active; active.openGate = gate; schedule(active);
    auto caller = asyncGet(2000); gate->awaitEntry();
    std::vector<std::future<int>> pending;
    for (int i = 0; i < 4; ++i) pending.push_back(asyncGet(2000));
    waitForQueue(4);
    auto started = std::chrono::steady_clock::now();
    assert(get(url()) == -1);
    assert(std::chrono::steady_clock::now() - started < 100ms);
    auto stopping = std::async(std::launch::async, [] { return stopHttpWorker(); });
    std::this_thread::sleep_for(30ms);
    assert(stopping.wait_for(0ms) != std::future_status::ready);
    assert(get(url()) == -1); // new work is rejected during shutdown
    gate->release();
    assert(stopping.get());
    assert(caller.get() == -1);
    for (auto &request : pending) assert(request.get() == -1);
    assert(snapshots().size() == 1 && liveClients == 0 && liveSemaphores == 0);
    assert(get(url()) == -1);
    reset();
    gate = std::make_shared<Gate>(); active.openGate = gate; schedule(active);
    caller = asyncGet(5000); gate->awaitEntry();
    assert(!stopHttpWorker()); // deliberately wedged driver cannot be killed safely
    assert(liveClients == 1);
    assert(get(url()) == -1);
    gate->release();
    assert(stopHttpWorker());
    assert(caller.get() == -1);
    reset();
    assert(get(url()) == 200); // a fully stopped worker can be restarted
}

void testAuthenticatedTime() {
    reset();
    uint64_t time = 123;
    assert(!getAuthenticatedServerTimeMs(nullptr));
    assert(!getAuthenticatedServerTimeMs(&time) && time == 0); // no Date header
    const std::string validDate = "Wed, 01 Jan 2025 00:00:00 GMT";
    Scenario valid; valid.headers = {{"dAtE", validDate}}; schedule(valid);
    assert(getAuthenticatedServerTimeMs(&time));
    assert(time >= 1735689600000ULL && time < 1735689601000ULL);
    assert(snapshots().back().url == BASE + "/api/health-check");
    for (const char *invalid : {"Wed, 01 Jan 2025 00:00:00 UTC", "Wed, 01 Jan 2025 00:00:00 GMT extra",
                               "Tue, 01 Jan 2025 00:00:00 GMT", "Thu, 29 Feb 2025 00:00:00 GMT",
                               "Wed, 00 Jan 2025 00:00:00 GMT", "Wed, 32 Jan 2025 00:00:00 GMT",
                               "Wed, 01 Jan 2025 24:00:00 GMT", "Wed, 01 Jan 2025 00:60:00 GMT",
                               "Wed, 01 Jan 2025 00:00:60 GMT", "Wed, 01 Jan 2100 00:00:00 GMT",
                               "Sun, 01 Jan 2023 00:00:00 GMT", "1735689600000",
                               "Wed, -1 Jan 2025 00:00:00 GMT", "Wed, 01 Xxx 2025 00:00:00 GMT"}) {
        Scenario bad; bad.headers = {{"Date", invalid}}; schedule(bad);
        time = 123;
        assert(!getAuthenticatedServerTimeMs(&time) && time == 0);
    }
    Scenario leap; leap.headers = {{"Date", "Thu, 29 Feb 2024 12:34:56 GMT"}}; schedule(leap);
    assert(getAuthenticatedServerTimeMs(&time));
    Scenario duplicate; duplicate.headers = {{"Date", validDate}, {"date", validDate}}; schedule(duplicate);
    assert(!getAuthenticatedServerTimeMs(&time) && time == 0);
    Scenario badStatus = valid; badStatus.status = 503; schedule(badStatus);
    assert(!getAuthenticatedServerTimeMs(&time) && time == 0);
    Scenario redirect = valid; redirect.status = 302; schedule(redirect);
    assert(!getAuthenticatedServerTimeMs(&time) && time == 0);
    Scenario invalidTls = valid; invalidTls.chainValid = false; schedule(invalidTls);
    assert(!getAuthenticatedServerTimeMs(&time) && time == 0);
    Scenario jsonOnly; jsonOnly.body = "{\"time\":1735689600000}"; schedule(jsonOnly);
    assert(!getAuthenticatedServerTimeMs(&time) && time == 0);
}

int main() {
    testTlsAndUrls();
    testBoundsAndRetry();
    testCallerTimeoutAndLateCompletion();
    testStopAndQueueSaturation();
    testAuthenticatedTime();
    assert(stopHttpWorker());
    for (auto &thread : workerThreads) thread.join();
    assert(liveClients == 0 && liveSemaphores == 0);
#ifdef VERSIONDEV
    std::cout << "HTTP worker security tests passed (Development)\n";
#else
    std::cout << "HTTP worker security tests passed (Production)\n";
#endif
}
