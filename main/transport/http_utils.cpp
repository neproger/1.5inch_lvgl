#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <strings.h>

#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "wifi_manager.h"
#include "http_utils.h"

static const char *TAG_HTTP = "http_utils";

esp_err_t http_send(const char *method,
                    const char *url,
                    const char *body,
                    const char *content_type_opt,
                    const char *auth_bearer_token_opt,
                    char *out,
                    size_t out_len,
                    int *out_status_code_opt)
{
    if (!method || !url)
        return ESP_ERR_INVALID_ARG;
    if (!wifi_manager_is_connected())
    {
        if (out_status_code_opt)
            *out_status_code_opt = 0;
        return ESP_ERR_INVALID_STATE;
    }

    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 15000;
    cfg.disable_auto_redirect = false;
    cfg.buffer_size = 2048;
    cfg.buffer_size_tx = 1024;
    cfg.keep_alive_enable = true;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client)
        return ESP_ERR_NO_MEM;

    esp_http_client_method_t m = HTTP_METHOD_GET;
    if (strcasecmp(method, "POST") == 0)
        m = HTTP_METHOD_POST;
    else if (strcasecmp(method, "PUT") == 0)
        m = HTTP_METHOD_PUT;
    else if (strcasecmp(method, "DELETE") == 0)
        m = HTTP_METHOD_DELETE;
#ifdef HTTP_METHOD_PATCH
    else if (strcasecmp(method, "PATCH") == 0)
        m = HTTP_METHOD_PATCH;
#endif
    esp_http_client_set_method(client, m);

    esp_http_client_set_header(client, "Accept", "application/json");
    esp_http_client_set_header(client, "Connection", "close");
    esp_http_client_set_header(client, "Accept-Encoding", "identity");
    if (auth_bearer_token_opt && *auth_bearer_token_opt)
    {
        char auth[384];
        snprintf(auth, sizeof(auth), "Bearer %s", auth_bearer_token_opt);
        esp_http_client_set_header(client, "Authorization", auth);
    }
    if (body)
    {
        const char *ct = content_type_opt ? content_type_opt : "application/json";
        esp_http_client_set_header(client, "Content-Type", ct);
    }

    size_t body_len = body ? std::strlen(body) : 0;
    esp_err_t err = esp_http_client_open(client, (int)body_len);
    if (err != ESP_OK)
    {
        esp_http_client_cleanup(client);
        return err;
    }
    if (body_len > 0)
    {
        int written = esp_http_client_write(client, body, (int)body_len);
        if (written < 0 || (size_t)written != body_len)
        {
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
    }

    (void)esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (out_status_code_opt)
        *out_status_code_opt = status;

    if (out && out_len > 0)
    {
        int total = 0;
        while (total < (int)out_len - 1)
        {
            int to_read = (int)out_len - 1 - total;
            if (to_read <= 0)
                break;
            int r = esp_http_client_read(client, out + total, to_read > 1024 ? 1024 : to_read);
            if (r <= 0)
                break;
            total += r;
        }
        out[total] = '\0';
    }

    ESP_LOGI(TAG_HTTP, "HTTP %s %s -> %d", method, url, status);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return ESP_OK;
}
