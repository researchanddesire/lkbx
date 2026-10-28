#pragma once
#include <cstdint>
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_FAIL = -1;
enum esp_http_client_method_t { HTTP_METHOD_GET, HTTP_METHOD_POST };
enum esp_http_client_transport_t { HTTP_TRANSPORT_UNKNOWN, HTTP_TRANSPORT_OVER_TCP, HTTP_TRANSPORT_OVER_SSL };
enum esp_http_client_event_id_t { HTTP_EVENT_ON_HEADER };
struct esp_http_client_event_t {
    esp_http_client_event_id_t event_id;
    void *user_data;
    const char *header_key;
    const char *header_value;
};
struct esp_http_client_config_t {
    const char *url = nullptr;
    esp_http_client_method_t method = HTTP_METHOD_GET;
    esp_http_client_transport_t transport_type = HTTP_TRANSPORT_UNKNOWN;
    int timeout_ms = 0, buffer_size = 0, buffer_size_tx = 0;
    bool disable_auto_redirect = false, skip_cert_common_name_check = false;
    const char *cert_pem = nullptr;
    int (*crt_bundle_attach)(void *) = nullptr;
    esp_err_t (*event_handler)(esp_http_client_event_t *) = nullptr;
    void *user_data = nullptr;
};
struct MockHttp;
using esp_http_client_handle_t = MockHttp *;
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char *, const char *);
esp_err_t esp_http_client_set_timeout_ms(esp_http_client_handle_t, int);
esp_err_t esp_http_client_open(esp_http_client_handle_t, int);
int esp_http_client_write(esp_http_client_handle_t, const char *, int);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t);
int esp_http_client_get_status_code(esp_http_client_handle_t);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t);
int esp_http_client_read(esp_http_client_handle_t, char *, int);
esp_err_t esp_http_client_close(esp_http_client_handle_t);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t);
