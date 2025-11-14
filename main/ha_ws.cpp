#include "ha_ws.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ha_ws_config.h"

namespace ha_ws
{

    namespace
    {
        static const char *TAG = "ha_ws";
        static esp_websocket_client_handle_t s_client = nullptr;
        static bool s_connected = false;
        static bool s_authenticated = false;
        static std::string s_frame_buffer;
        static size_t s_frame_expected = 0;
        static std::atomic<int> s_next_id{1};
        static MessageHandler s_handler = nullptr;

        esp_err_t send_text(const char *data, size_t len)
        {
            if (!s_client || !is_connected())
                return ESP_ERR_INVALID_STATE;
            if (!data || len == 0)
                return ESP_ERR_INVALID_ARG;
            int ret = esp_websocket_client_send_text(s_client, data, (int)len, pdMS_TO_TICKS(4000));
            return (ret >= 0) ? ESP_OK : ESP_FAIL;
        }

        esp_err_t send_json(cJSON *obj)
        {
            if (!obj)
                return ESP_ERR_INVALID_ARG;
            char *json = cJSON_PrintUnformatted(obj);
            if (!json)
                return ESP_ERR_NO_MEM;
            size_t len = std::strlen(json);
            esp_err_t err = send_text(json, len);
            cJSON_free(json);
            return err;
        }

        esp_err_t request_simple(const char *type)
        {
            if (!type || !*type)
                return ESP_ERR_INVALID_ARG;
            if (!is_ready())
                return ESP_ERR_INVALID_STATE;
            cJSON *root = cJSON_CreateObject();
            if (!root)
                return ESP_ERR_NO_MEM;
            cJSON_AddNumberToObject(root, "id", s_next_id.fetch_add(1));
            cJSON_AddStringToObject(root, "type", type);
            esp_err_t err = send_json(root);
            cJSON_Delete(root);
            if (err == ESP_OK)
            {
                ESP_LOGI(TAG, "WS TX type='%s'", type);
            }
            return err;
        }

        void send_auth()
        {
            if (!HA_WS_ACCESS_TOKEN[0])
            {
                ESP_LOGE(TAG, "HA_WS_ACCESS_TOKEN is empty; cannot authenticate");
                return;
            }
            char payload[512];
            int written = snprintf(payload, sizeof(payload),
                                   "{\"type\":\"auth\",\"access_token\":\"%s\"}",
                                   HA_WS_ACCESS_TOKEN);
            if (written <= 0 || written >= (int)sizeof(payload))
            {
                ESP_LOGE(TAG, "Auth payload truncated; increase buffer");
                return;
            }
            if (send_text(payload, (size_t)written) == ESP_OK)
            {
                ESP_LOGI(TAG, "WS -> auth");
            }
        }

        void deliver_application_message(const char *json, size_t len)
        {
            if (s_handler)
            {
                s_handler(json, len);
            }
            else
            {
                ESP_LOGI(TAG, "WS RX %.*s", (int)len, json);
            }
        }

        void process_message(const char *json, size_t len)
        {
            cJSON *root = cJSON_ParseWithLength(json, len);
            if (!root)
            {
                ESP_LOGW(TAG, "WS RX invalid JSON");
                return;
            }
            const cJSON *type_item = cJSON_GetObjectItem(root, "type");
            const char *type = cJSON_IsString(type_item) ? type_item->valuestring : nullptr;
            if (type)
            {
                if (strcmp(type, "auth_required") == 0)
                {
                    ESP_LOGI(TAG, "WS auth required");
                    send_auth();
                }
                else if (strcmp(type, "auth_ok") == 0)
                {
                    s_authenticated = true;
                    ESP_LOGI(TAG, "WS auth ok");
                }
                else if (strcmp(type, "auth_invalid") == 0)
                {
                    s_authenticated = false;
                    const cJSON *msg = cJSON_GetObjectItem(root, "message");
                    ESP_LOGE(TAG, "WS auth invalid: %s",
                             cJSON_IsString(msg) ? msg->valuestring : "unknown reason");
                }
                else
                {
                    deliver_application_message(json, len);
                }
            }
            else
            {
                deliver_application_message(json, len);
            }
            cJSON_Delete(root);
        }

        void reset_rx_state()
        {
            s_frame_buffer.clear();
            s_frame_expected = 0;
        }

        void ws_event_handler(void * /*handler_args*/, esp_event_base_t /*base*/, int32_t event_id, void *event_data)
        {
            auto *event = static_cast<esp_websocket_event_data_t *>(event_data);
            switch (event_id)
            {
            case WEBSOCKET_EVENT_CONNECTED:
                s_connected = true;
                ESP_LOGI(TAG, "Connected to %s", HA_WS_URI);
                break;
            case WEBSOCKET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Disconnected from HA WS");
                s_connected = false;
                s_authenticated = false;
                reset_rx_state();
                break;
            case WEBSOCKET_EVENT_DATA:
                if (!event)
                    break;
                if (event->payload_offset == 0)
                {
                    s_frame_expected = event->payload_len;
                    s_frame_buffer.clear();
                    if (s_frame_expected > 0)
                        s_frame_buffer.reserve(s_frame_expected);
                }
                if (event->data_len > 0 && event->data_ptr)
                {
                    s_frame_buffer.append(reinterpret_cast<const char *>(event->data_ptr), event->data_len);
                }
                if (s_frame_expected > 0 && s_frame_buffer.size() >= s_frame_expected)
                {
                    process_message(s_frame_buffer.c_str(), s_frame_buffer.size());
                    reset_rx_state();
                }
                break;
            case WEBSOCKET_EVENT_ERROR:
                ESP_LOGE(TAG, "WebSocket error");
                break;
            default:
                break;
            }
        }
    } // namespace

    esp_err_t start()
    {
        if (s_client)
            return ESP_OK;

        esp_websocket_client_config_t cfg = {};
        cfg.uri = HA_WS_URI;
        cfg.subprotocol = "homeassistant";
        cfg.task_name = "ha_ws";
        cfg.task_prio = 5;
        cfg.task_stack = 2048;
        cfg.buffer_size = 2048;
        cfg.disable_auto_reconnect = false;
        cfg.reconnect_timeout_ms = 3000;

        s_client = esp_websocket_client_init(&cfg);
        if (!s_client)
            return ESP_ERR_NO_MEM;

        esp_err_t err = esp_websocket_register_events(s_client,
                                                      WEBSOCKET_EVENT_ANY,
                                                      ws_event_handler,
                                                      nullptr);
        if (err != ESP_OK)
        {
            esp_websocket_client_destroy(s_client);
            s_client = nullptr;
            return err;
        }

        err = esp_websocket_client_start(s_client);
        if (err != ESP_OK)
        {
            esp_websocket_client_destroy(s_client);
            s_client = nullptr;
            ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "WS client starting -> %s", HA_WS_URI);
        return ESP_OK;
    }

    bool is_connected()
    {
        return s_connected;
    }

    bool is_authenticated()
    {
        return s_authenticated;
    }

    bool is_ready()
    {
        return s_connected && s_authenticated;
    }

    void set_message_handler(MessageHandler handler)
    {
        s_handler = handler;
    }

    esp_err_t request_area_registry_list()
    {
        return request_simple("config/area_registry/list");
    }

    esp_err_t request_device_registry_list()
    {
        return request_simple("config/device_registry/list");
    }

    esp_err_t request_entity_registry_list()
    {
        return request_simple("config/entity_registry/list");
    }

} // namespace ha_ws
