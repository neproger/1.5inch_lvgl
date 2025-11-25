#include "config_server.hpp"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"

#include "config_store.hpp"
#include "cJSON.h"

#include <string>

namespace config_server
{

    namespace
    {
        const char *TAG = "cfg_http";

        // Embedded static files (configured via EMBED_TXTFILES in CMakeLists.txt)
        // Symbol names are derived from filenames: index.html -> _binary_index_html_start, etc.
        extern const unsigned char index_html_start[] asm("_binary_index_html_start");
        extern const unsigned char index_html_end[] asm("_binary_index_html_end");

        extern const unsigned char style_css_start[] asm("_binary_style_css_start");
        extern const unsigned char style_css_end[] asm("_binary_style_css_end");

        httpd_handle_t s_httpd = nullptr;

        esp_err_t send_static(httpd_req_t *req, const unsigned char *start, const unsigned char *end, const char *content_type)
        {
            httpd_resp_set_type(req, content_type);
            const size_t len = static_cast<size_t>(end - start);
            return httpd_resp_send(req, reinterpret_cast<const char *>(start), len);
        }

        esp_err_t handle_root(httpd_req_t *req)
        {
            return send_static(req,
                               index_html_start,
                               index_html_end,
                               "text/html");
        }

        esp_err_t handle_style_css(httpd_req_t *req)
        {
            return send_static(req,
                               style_css_start,
                               style_css_end,
                               "text/css");
        }

        esp_err_t handle_get_config(httpd_req_t *req)
        {
            config_store::WifiAp aps[8];
            config_store::HaConn ha[4];
            std::size_t wifi_count = 0;
            std::size_t ha_count = 0;
            (void)config_store::load_wifi(aps, 8, wifi_count);
            (void)config_store::load_ha(ha, 4, ha_count);

            // Very small hand-written JSON to avoid extra dependencies.
            httpd_resp_set_type(req, "application/json");
            std::string json = "{ \"wifi\": [";
            for (std::size_t i = 0; i < wifi_count; ++i)
            {
                if (i > 0)
                    json += ',';
                json += "{\"ssid\":\"";
                json += aps[i].ssid;
                json += "\",\"password\":\"";
                json += aps[i].password;
                json += "\"}";
            }
            json += "],\"ha\":[";
            for (std::size_t i = 0; i < ha_count; ++i)
            {
                if (i > 0)
                    json += ',';
                json += "{\"host\":\"";
                json += ha[i].host;
                json += "\",\"http_port\":";
                json += std::to_string(ha[i].http_port);
                json += ",\"mqtt_port\":";
                json += std::to_string(ha[i].mqtt_port);
                json += ",\"mqtt_username\":\"";
                json += ha[i].mqtt_username;
                json += "\",\"mqtt_password\":\"";
                json += ha[i].mqtt_password;
                json += "\",\"http_token\":\"";
                json += ha[i].http_token;
                json += "\"}";
            }
            json += "]}";

            return httpd_resp_send(req, json.c_str(), json.size());
        }

        esp_err_t handle_post_config(httpd_req_t *req)
        {
            const size_t content_len = static_cast<size_t>(req->content_len);
            if (content_len == 0 || content_len > 2048)
            {
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body length");
            }

            char *buf = static_cast<char *>(malloc(content_len + 1));
            if (!buf)
            {
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory");
            }

            size_t received = 0;
            while (received < content_len)
            {
                const int r = httpd_req_recv(req, buf + received, content_len - received);
                if (r <= 0)
                {
                    free(buf);
                    return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Failed to read body");
                }
                received += static_cast<size_t>(r);
            }
            buf[received] = '\0';

            ESP_LOGI(TAG, "Received config body (%u bytes)", static_cast<unsigned>(received));

            cJSON *root = cJSON_Parse(buf);
            free(buf);
            if (!root)
            {
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
            }

            config_store::WifiAp wifi_items[8];
            config_store::HaConn ha_items[4];
            std::size_t wifi_count = 0;
            std::size_t ha_count = 0;

            cJSON *wifi = cJSON_GetObjectItem(root, "wifi");
            if (wifi && cJSON_IsArray(wifi))
            {
                const int arr_size = cJSON_GetArraySize(wifi);
                for (int i = 0; i < arr_size && wifi_count < 8; ++i)
                {
                    cJSON *item = cJSON_GetArrayItem(wifi, i);
                    if (!cJSON_IsObject(item))
                        continue;
                    cJSON *ssid = cJSON_GetObjectItem(item, "ssid");
                    cJSON *password = cJSON_GetObjectItem(item, "password");
                    if (!cJSON_IsString(ssid) || ssid->valuestring == nullptr)
                        continue;
                    config_store::WifiAp ap{};
                    strncpy(ap.ssid, ssid->valuestring, sizeof(ap.ssid) - 1);
                    if (cJSON_IsString(password) && password->valuestring)
                    {
                        strncpy(ap.password, password->valuestring, sizeof(ap.password) - 1);
                    }
                    wifi_items[wifi_count++] = ap;
                }
            }

            cJSON *ha = cJSON_GetObjectItem(root, "ha");
            if (ha && cJSON_IsArray(ha))
            {
                const int arr_size = cJSON_GetArraySize(ha);
                for (int i = 0; i < arr_size && ha_count < 4; ++i)
                {
                    cJSON *item = cJSON_GetArrayItem(ha, i);
                    if (!cJSON_IsObject(item))
                        continue;

                    cJSON *host = cJSON_GetObjectItem(item, "host");
                    if (!cJSON_IsString(host) || host->valuestring == nullptr)
                        continue;

                    cJSON *http_port = cJSON_GetObjectItem(item, "http_port");
                    cJSON *mqtt_port = cJSON_GetObjectItem(item, "mqtt_port");
                    cJSON *mqtt_username = cJSON_GetObjectItem(item, "mqtt_username");
                    cJSON *mqtt_password = cJSON_GetObjectItem(item, "mqtt_password");
                    cJSON *http_token = cJSON_GetObjectItem(item, "http_token");

                    config_store::HaConn conn{};
                    strncpy(conn.host, host->valuestring, sizeof(conn.host) - 1);
                    conn.http_port = static_cast<uint16_t>(cJSON_IsNumber(http_port) ? http_port->valuedouble : 8123);
                    conn.mqtt_port = static_cast<uint16_t>(cJSON_IsNumber(mqtt_port) ? mqtt_port->valuedouble : 1883);
                    if (cJSON_IsString(mqtt_username) && mqtt_username->valuestring)
                    {
                        strncpy(conn.mqtt_username, mqtt_username->valuestring, sizeof(conn.mqtt_username) - 1);
                    }
                    if (cJSON_IsString(mqtt_password) && mqtt_password->valuestring)
                    {
                        strncpy(conn.mqtt_password, mqtt_password->valuestring, sizeof(conn.mqtt_password) - 1);
                    }
                    if (cJSON_IsString(http_token) && http_token->valuestring)
                    {
                        strncpy(conn.http_token, http_token->valuestring, sizeof(conn.http_token) - 1);
                    }
                    ha_items[ha_count++] = conn;
                }
            }

            cJSON_Delete(root);

            esp_err_t err = config_store::save_wifi(wifi_items, wifi_count);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "save_wifi failed: %s", esp_err_to_name(err));
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save WiFi");
            }

            err = config_store::save_ha(ha_items, ha_count);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "save_ha failed: %s", esp_err_to_name(err));
                return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save HA");
            }

            httpd_resp_set_type(req, "application/json");
            const char *ok = "{\"status\":\"ok\"}";
            return httpd_resp_send(req, ok, strlen(ok));
        }

        esp_err_t handle_reboot(httpd_req_t *req)
        {
            (void)req;
            httpd_resp_set_type(req, "application/json");
            const char *body = "{\"status\":\"rebooting\"}";
            // Try to send response before restarting
            (void)httpd_resp_send(req, body, strlen(body));
            ESP_LOGI(TAG, "Reboot requested via HTTP config UI");
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
            return ESP_OK;
        }

    } // namespace

    esp_err_t start()
    {
        if (s_httpd)
        {
            return ESP_OK;
        }

        httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
        cfg.server_port = 80;
        cfg.uri_match_fn = httpd_uri_match_wildcard;
        // Increase stack size to handle JSON parsing comfortably
        cfg.stack_size = 8192;

        esp_err_t err = httpd_start(&s_httpd, &cfg);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
            s_httpd = nullptr;
            return err;
        }

        httpd_uri_t root = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = handle_root,
            .user_ctx = nullptr,
        };
        httpd_register_uri_handler(s_httpd, &root);

        httpd_uri_t css = {
            .uri = "/style.css",
            .method = HTTP_GET,
            .handler = handle_style_css,
            .user_ctx = nullptr,
        };
        httpd_register_uri_handler(s_httpd, &css);

        httpd_uri_t get_cfg = {
            .uri = "/api/config",
            .method = HTTP_GET,
            .handler = handle_get_config,
            .user_ctx = nullptr,
        };
        httpd_register_uri_handler(s_httpd, &get_cfg);

        httpd_uri_t post_cfg = {
            .uri = "/api/config",
            .method = HTTP_POST,
            .handler = handle_post_config,
            .user_ctx = nullptr,
        };
        httpd_register_uri_handler(s_httpd, &post_cfg);

        httpd_uri_t reboot = {
            .uri = "/api/reboot",
            .method = HTTP_POST,
            .handler = handle_reboot,
            .user_ctx = nullptr,
        };
        httpd_register_uri_handler(s_httpd, &reboot);

        ESP_LOGI(TAG, "Config HTTP server started on port %d", cfg.server_port);
        return ESP_OK;
    }

    void stop()
    {
        if (s_httpd)
        {
            httpd_stop(s_httpd);
            s_httpd = nullptr;
        }
    }

} // namespace config_server
