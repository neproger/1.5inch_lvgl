#include "config_server.hpp"

#include "esp_http_server.h"
#include "esp_log.h"

#include "config_store.hpp"

namespace config_server
{

    namespace
    {
        const char *TAG = "cfg_http";

        // Embedded static files (configured via EMBED_TXTFILES in CMakeLists.txt)
        extern const unsigned char _binary_config_server_www_index_html_start[] asm("_binary_config_server_www_index_html_start");
        extern const unsigned char _binary_config_server_www_index_html_end[] asm("_binary_config_server_www_index_html_end");

        extern const unsigned char _binary_config_server_www_app_js_start[] asm("_binary_config_server_www_app_js_start");
        extern const unsigned char _binary_config_server_www_app_js_end[] asm("_binary_config_server_www_app_js_end");

        extern const unsigned char _binary_config_server_www_style_css_start[] asm("_binary_config_server_www_style_css_start");
        extern const unsigned char _binary_config_server_www_style_css_end[] asm("_binary_config_server_www_style_css_end");

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
                               _binary_config_server_www_index_html_start,
                               _binary_config_server_www_index_html_end,
                               "text/html");
        }

        esp_err_t handle_app_js(httpd_req_t *req)
        {
            return send_static(req,
                               _binary_config_server_www_app_js_start,
                               _binary_config_server_www_app_js_end,
                               "application/javascript");
        }

        esp_err_t handle_style_css(httpd_req_t *req)
        {
            return send_static(req,
                               _binary_config_server_www_style_css_start,
                               _binary_config_server_www_style_css_end,
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
            // For now accept simple form-urlencoded payload of fixed size.
            // Later we can replace with a tiny JSON parser if needed.
            char buf[1024];
            int total = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (total <= 0)
            {
                return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty body");
            }
            buf[total] = '\0';

            // TODO: parse payload; for now just log and respond OK.
            ESP_LOGI(TAG, "Received config body (%d bytes)", total);

            httpd_resp_set_type(req, "application/json");
            const char *ok = "{\"status\":\"ok\"}";
            return httpd_resp_send(req, ok, strlen(ok));
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

        httpd_uri_t js = {
            .uri = "/app.js",
            .method = HTTP_GET,
            .handler = handle_app_js,
            .user_ctx = nullptr,
        };
        httpd_register_uri_handler(s_httpd, &js);

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

