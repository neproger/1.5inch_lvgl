#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_http_client.h"
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ha_client.h"
#include "ha_client_config.h"
#include "wifi_manager.h"

static const char *TAG = "ha_client";
static volatile bool s_online = false;           // last known HA availability
static volatile int64_t s_last_ok_us = 0;        // last successful HTTP ts
static volatile bool s_http_in_flight = false;   // any HTTP request is in progress
static volatile int64_t s_last_activity_us = 0;  // last HTTP start time
static volatile int64_t s_no_status_until_us = 0;// backoff deadline for status probes

// Stored configuration
static char s_base_url[128] = {0};
static char s_token[256]    = {0};
static const char *s_ca_cert = NULL; // PEM for HTTPS (optional)

static bool is_https_url(const char *url)
{
    return (url && strncasecmp(url, "https://", 8) == 0);
}

static esp_err_t ensure_initialized(void)
{
    if (s_base_url[0] == '\0' || s_token[0] == '\0') {
        snprintf(s_base_url, sizeof(s_base_url), "%s", HA_DEFAULT_BASE_URL);
        snprintf(s_token,    sizeof(s_token),    "%s", HA_DEFAULT_TOKEN);
        s_ca_cert = HA_DEFAULT_CA_CERT;
    }
    if (s_base_url[0] == '\0' || s_token[0] == '\0') {
        ESP_LOGE(TAG, "HA client not initialized and defaults are empty");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t ha_client_init(const char *base_url, const char *token, const char *ca_cert_pem)
{
    if (!base_url || !token) return ESP_ERR_INVALID_ARG;
    snprintf(s_base_url, sizeof(s_base_url), "%s", base_url);
    snprintf(s_token,    sizeof(s_token),    "%s", token);
    s_ca_cert = ca_cert_pem; // may be NULL
    ESP_LOGI(TAG, "HA client configured: %s", s_base_url);
    return ESP_OK;
}

static esp_err_t http_request(const char *method, const char *path,
                              const char *body,
                              char *out, size_t out_len,
                              int *out_status_code)
{
    // Skip if WiFi is not connected
    if (!wifi_manager_is_connected()) {
        if (out_status_code) *out_status_code = 0;
        ESP_LOGW(TAG, "WiFi not connected, skip HTTP %s %s", method ? method : "?", path ? path : "?");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ensure_initialized();
    if (err != ESP_OK) return err;

    if (!method || !path) return ESP_ERR_INVALID_ARG;

    size_t full_len = strlen(s_base_url) + strlen(path) + 2;
    char *url = (char *)malloc(full_len);
    if (!url) return ESP_ERR_NO_MEM;
    snprintf(url, full_len, "%s%s", s_base_url, path);

    // Build client config (no designated initializers in C++)
    esp_http_client_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.url = url;
    cfg.timeout_ms = 7000;
    cfg.disable_auto_redirect = false;
    cfg.buffer_size = 2048;      // increase RX buffer for headers/body chunks
    cfg.buffer_size_tx = 1024;   // increase TX buffer for small JSON bodies
    cfg.keep_alive_enable = true;

    if (is_https_url(url)) {
        if (s_ca_cert && s_ca_cert[0] != '\0') {
            cfg.cert_pem = s_ca_cert;
        } else {
        #if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
            cfg.crt_bundle_attach = esp_crt_bundle_attach;
        #else
            ESP_LOGW(TAG, "HTTPS: no CA provided and certificate bundle disabled. Provide ca_cert or enable bundle, or use HTTP.");
        #endif
        }
        cfg.skip_cert_common_name_check = true;
    }

    int64_t now_us = esp_timer_get_time();
    s_last_activity_us = now_us;
    if (method && strcasecmp(method, "GET") != 0) {
        const int64_t STATUS_BACKOFF_US = 1500 * 1000; // 1.5s
        s_no_status_until_us = now_us + STATUS_BACKOFF_US;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(url);
        return ESP_ERR_NO_MEM;
    }

    esp_http_client_set_method(client,
        strcasecmp(method, "GET") == 0 ? HTTP_METHOD_GET :
        strcasecmp(method, "POST") == 0 ? HTTP_METHOD_POST :
        strcasecmp(method, "PUT") == 0 ? HTTP_METHOD_PUT :
        strcasecmp(method, "DELETE") == 0 ? HTTP_METHOD_DELETE : HTTP_METHOD_GET);

    char auth[320];
    snprintf(auth, sizeof(auth), "Bearer %s", s_token);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Connection", "close");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    if (body) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, body, strlen(body));
    }

    // Explicit open/write/fetch/read path for robust body retrieval
    size_t body_len = body ? strlen(body) : 0;
    int64_t t0_us = esp_timer_get_time();
    ESP_LOGI(TAG, "HTTP %s %s body=%uB", method, url, (unsigned)body_len);

    s_http_in_flight = true;
    err = esp_http_client_open(client, (int)body_len);
    if (err != ESP_OK) {
        int64_t dt_us = esp_timer_get_time() - t0_us;
        ESP_LOGE(TAG, "HTTP open %s %s -> err=%s (%.1f ms)",
                 method, url, esp_err_to_name(err), (double)dt_us/1000.0);
        s_http_in_flight = false;
        esp_http_client_cleanup(client);
        free(url);
        return err;
    }
    if (body_len > 0) {
        int written = esp_http_client_write(client, body, (int)body_len);
        if (written < 0 || (size_t)written != body_len) {
            ESP_LOGE(TAG, "HTTP write failed (%d/%u)", written, (unsigned)body_len);
            esp_http_client_close(client);
            s_http_in_flight = false;
            esp_http_client_cleanup(client);
            free(url);
            return ESP_FAIL;
        }
    }

    int fetch_len = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (out_status_code) *out_status_code = status;

    if (out && out_len > 0) {
        int total_read = 0;
        while (1) {
            int remain = (int)(out_len - 1 - total_read);
            if (remain <= 0) break;
            int r = esp_http_client_read(client, out + total_read, remain > 1024 ? 1024 : remain);
            if (r <= 0) break; // 0 on EOF
            total_read += r;
        }
        out[total_read] = '\0';
        int64_t dt_us = esp_timer_get_time() - t0_us;
        int content_len = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP %s %s -> %d, recv=%dB (%.1f ms) len=%d fetch=%d",
                 method, url, status, total_read, (double)dt_us/1000.0, content_len, fetch_len);
    } else {
        int64_t dt_us = esp_timer_get_time() - t0_us;
        int content_len = esp_http_client_get_content_length(client);
        ESP_LOGI(TAG, "HTTP %s %s -> %d, len=%d (%.1f ms)",
                 method, url, status, content_len, (double)dt_us/1000.0);
    }

    esp_http_client_close(client);

    if (status >= 200 && status < 300) { s_last_ok_us = esp_timer_get_time(); s_online = true; }
    esp_http_client_cleanup(client);
    free(url);
    s_http_in_flight = false;
    return ESP_OK;
}

esp_err_t ha_get_status(int *http_status_opt)
{
    char buf[64];
    int code = 0;
    esp_err_t err = http_request("GET", "/api/", NULL, buf, sizeof(buf), &code);
    if (http_status_opt) *http_status_opt = code;
    return err;
}

esp_err_t ha_get_state(const char *entity_id,
                       char *resp_buf, size_t resp_buf_len,
                       int *http_status_opt)
{
    if (!entity_id) return ESP_ERR_INVALID_ARG;
    char path[256];
    snprintf(path, sizeof(path), "/api/states/%s", entity_id);
    return http_request("GET", path, NULL, resp_buf, resp_buf_len, http_status_opt);
}

esp_err_t ha_call_service(const char *domain, const char *service,
                          const char *json_body,
                          char *resp_buf, size_t resp_buf_len,
                          int *http_status_opt)
{
    if (!domain || !service) return ESP_ERR_INVALID_ARG;
    char path[256];
    snprintf(path, sizeof(path), "/api/services/%s/%s", domain, service);
    return http_request("POST", path, json_body, resp_buf, resp_buf_len, http_status_opt);
}

static bool split_domain_from_entity(const char *entity_id, char *domain_out, size_t domain_len)
{
    const char *dot = entity_id ? strchr(entity_id, '.') : NULL;
    if (!dot) return false;
    size_t n = (size_t)(dot - entity_id);
    if (n + 1 > domain_len) return false;
    memcpy(domain_out, entity_id, n);
    domain_out[n] = '\0';
    return true;
}

esp_err_t ha_toggle(const char *entity_id, int *http_status_opt)
{
    if (!entity_id) return ESP_ERR_INVALID_ARG;
    char domain[32];
    if (!split_domain_from_entity(entity_id, domain, sizeof(domain))) {
        return ESP_ERR_INVALID_ARG;
    }
    char body[128];
    snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", entity_id);
    return ha_call_service(domain, "toggle", body, NULL, 0, http_status_opt);
}

bool ha_client_is_online(void)
{
    int64_t now = esp_timer_get_time();
    if (s_last_ok_us == 0) return false;
    bool fresh = (now - s_last_ok_us) < (30LL * 1000 * 1000);
    if (!fresh) s_online = false;
    return fresh && s_online;
}

int64_t ha_client_get_last_ok_us(void)
{
    return s_last_ok_us;
}

bool ha_client_is_busy(void)
{
    return s_http_in_flight;
}

int64_t ha_client_get_last_activity_us(void)
{
    return s_last_activity_us;
}

int64_t ha_client_get_no_status_until_us(void)
{
    return s_no_status_until_us;
}
