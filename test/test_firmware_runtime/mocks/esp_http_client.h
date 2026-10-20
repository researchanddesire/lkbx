#pragma once
#include <cstdint>
constexpr int ESP_OK = 0;
enum { HTTP_METHOD_GET, HTTP_METHOD_POST };
struct esp_http_client_config_t {
    const char* url = nullptr;
    int method = 0, timeout_ms = 0, buffer_size = 0, buffer_size_tx = 0;
    int (*crt_bundle_attach)(void*) = nullptr;
    bool skip_cert_common_name_check = false;
};
struct HttpMock;
using esp_http_client_handle_t = HttpMock*;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*);
int esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*);
int esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_write(esp_http_client_handle_t, const char*, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char*, int);
int esp_http_client_close(esp_http_client_handle_t);
int esp_http_client_cleanup(esp_http_client_handle_t);
