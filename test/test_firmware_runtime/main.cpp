#include "FirmwareUpdateRuntime.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>

struct HttpMock {
    std::string body = "abc";
    int status = 200;
    int64_t length = 3;
    bool trustedCertificate = true, readFailure = false;
    size_t offset = 0;
    int closed = 0, cleaned = 0;
} http;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
    assert(config->crt_bundle_attach && !config->skip_cert_common_name_check);
    assert(std::string(config->url).rfind("https://", 0) == 0);
    return &http;
}
int esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*) { return 0; }
int esp_http_client_open(esp_http_client_handle_t, int) { return http.trustedCertificate ? ESP_OK : -1; }
int esp_http_client_write(esp_http_client_handle_t, const char*, int size) { return size; }
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t) { return http.length; }
int esp_http_client_get_status_code(esp_http_client_handle_t) { return http.status; }
int esp_http_client_read(esp_http_client_handle_t, char* destination, int size) {
    if (http.offset == http.body.size()) return http.readFailure ? -1 : 0;
    const auto count = std::min(size_t(size), http.body.size() - http.offset);
    std::memcpy(destination, http.body.data() + http.offset, count);
    http.offset += count;
    return int(count);
}
int esp_http_client_close(esp_http_client_handle_t) { ++http.closed; return 0; }
int esp_http_client_cleanup(esp_http_client_handle_t) { ++http.cleaned; return 0; }

firmware::Artifact artifact() {
    http = {}; Update = {};
    firmware::Artifact result;
    result.url = "https://example.supabase.co/storage/v1/object/public/lkbx-firmware/releases/synthetic/firmware.bin";
    result.sizeBytes = 3;
    result.sha256 = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    return result;
}
bool install(const firmware::Artifact& input) {
    String error;
    const bool result = firmware::installStreamedArtifact(input, U_FLASH, error);
    assert(http.closed == 1 && http.cleaned == 1);
    if (!result) assert(!error.isEmpty());
    return result;
}
int main() {
    auto input = artifact();
    assert(install(input) && Update.finalizeCalls == 1 && Update.abortCalls == 0);
    input = artifact(); input.sha256[0] = '0';
    assert(!install(input) && Update.finalizeCalls == 0 && Update.abortCalls == 1);
    input = artifact(); http.body = "abd";
    assert(!install(input) && Update.finalizeCalls == 0 && Update.abortCalls == 1);
    input = artifact(); http.body = "ab"; http.length = -1;
    assert(!install(input) && Update.finalizeCalls == 0 && Update.abortCalls == 1);
    input = artifact(); http.body = "abcd"; http.length = -1;
    assert(!install(input) && Update.finalizeCalls == 0 && Update.abortCalls == 1);
    input = artifact(); http.length = 4;
    assert(!install(input) && Update.beginCalls == 0 && Update.finalizeCalls == 0);
    input = artifact(); http.readFailure = true;
    assert(!install(input) && Update.finalizeCalls == 0 && Update.abortCalls == 1);
    input = artifact(); http.trustedCertificate = false;
    assert(!install(input) && Update.beginCalls == 0 && Update.finalizeCalls == 0);
    input = artifact(); http.status = 302;
    assert(!install(input) && Update.beginCalls == 0 && Update.finalizeCalls == 0);
    input = artifact(); Update.writeOk = false;
    assert(!install(input) && Update.finalizeCalls == 0 && Update.abortCalls == 1);
    std::cout << "10 actual OTA stream verifier cases passed; real SHA-256, mocked transport/flash\n";
}
