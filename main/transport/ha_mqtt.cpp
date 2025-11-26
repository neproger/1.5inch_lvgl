#include <cstring>
#include <cstdio>
#include <string>

#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

#include "ha_mqtt.hpp"
#include "ha_mqtt_config.h"
#include "config_server/config_store.hpp"
#include "http_manager.hpp"

namespace ha_mqtt
{

    static const char *TAG = "ha_mqtt";
    static esp_mqtt_client_handle_t s_client = nullptr;
    static volatile bool s_connected = false;
    static MessageHandler s_handler = nullptr;

    // Runtime MQTT connection parameters (backed by config_store or compile-time defaults)
    static std::string s_uri;
    static std::string s_user;
    static std::string s_pass;
    static std::string s_client_id;
    static std::string s_host;
    static std::uint16_t s_port = 0;

    // Simple subscription registry (re-subscribed on reconnect)
    struct sub_item_t
    {
        char topic[96];
        int qos;
    };
    static sub_item_t s_subs[8];
    static int s_subs_count = 0;

    static void publish_status(const char *status)
    {
        if (!s_client)
            return;
        if (!status)
            status = "unknown";
        (void)esp_mqtt_client_publish(s_client, HA_MQTT_STATUS_TOPIC, status, 0, 1, true);
    }

    static void on_mqtt_event(void * /*handler_args*/, esp_event_base_t /*base*/, int32_t /*event_id*/, void *event_data)
    {
        auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
        switch (event->event_id)
        {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "Connected to broker");
            publish_status("online");
            // Re-subscribe on reconnect.
            for (int i = 0; i < s_subs_count; ++i)
            {
                int mid = esp_mqtt_client_subscribe(s_client, s_subs[i].topic, s_subs[i].qos);
                ESP_LOGI(TAG, "SUB %s qos=%d mid=%d", s_subs[i].topic, s_subs[i].qos, mid);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG,
                     "Disconnected from broker (uri=%s host=%s port=%u)",
                     s_uri.c_str(),
                     s_host.empty() ? "-" : s_host.c_str(),
                     static_cast<unsigned>(s_port));
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG,
                     "MQTT error (uri=%s host=%s port=%u)",
                     s_uri.c_str(),
                     s_host.empty() ? "-" : s_host.c_str(),
                     static_cast<unsigned>(s_port));
            break;
        case MQTT_EVENT_DATA:
            if (s_handler && event->topic && event->topic_len > 0)
            {
                // esp-mqtt provides topic with explicit length, not null-terminated.
                // Our handler contract expects a null-terminated topic string.
                size_t tlen = static_cast<size_t>(event->topic_len);
                char topic_buf[192];
                if (tlen >= sizeof(topic_buf))
                    tlen = sizeof(topic_buf) - 1;
                std::memcpy(topic_buf, event->topic, tlen);
                topic_buf[tlen] = '\0';
                s_handler(topic_buf, event->data, event->data_len);
            }
            break;
        default:
            break;
        }
    }

    esp_err_t start()
    {
        if (s_client)
            return ESP_OK;

        // Pick configuration: prefer NVS (config_store), fall back to compile-time.
        config_store::HaConn ha_items[4];
        std::size_t ha_count = 0;
        if (config_store::load_ha(ha_items, 4, ha_count) == ESP_OK && ha_count > 0)
        {
            const config_store::HaConn *chosen = nullptr;

            // Try to match HA entry that was successfully used for HTTP (bootstrap/weather).
            std::string last_http_host;
            std::uint16_t last_http_port = 0;
            bool have_last_http = http_manager::get_last_successful_http_host(last_http_host, last_http_port);

            if (have_last_http)
            {
                for (std::size_t i = 0; i < ha_count; ++i)
                {
                    if (ha_items[i].host[0] != '\0' &&
                        last_http_host == ha_items[i].host)
                    {
                        chosen = &ha_items[i];
                        break;
                    }
                }
            }

            // Fallback: first non-empty host if we don't have / can't match HTTP.
            if (!chosen)
            {
                for (std::size_t i = 0; i < ha_count; ++i)
                {
                    if (ha_items[i].host[0] != '\0')
                    {
                        chosen = &ha_items[i];
                        break;
                    }
                }
            }

            if (chosen)
            {
                const auto &c = *chosen;
                const uint16_t mqtt_port = c.mqtt_port ? c.mqtt_port : 1883;

                s_uri = "mqtt://";
                s_uri += c.host;
                s_uri += ":";
                s_uri += std::to_string(mqtt_port);
                s_user = c.mqtt_username;
                s_pass = c.mqtt_password;
                s_client_id = HA_MQTT_CLIENT_ID;
                s_host = c.host;
                s_port = mqtt_port;

                ESP_LOGI(TAG,
                         "Using MQTT from HA config: host=%s http_port=%u mqtt_port=%u user='%s' (matched_http=%s)",
                         c.host,
                         static_cast<unsigned>(c.http_port),
                         static_cast<unsigned>(mqtt_port),
                         c.mqtt_username,
                         have_last_http ? "yes" : "no");
            }
            else
            {
                ESP_LOGW(TAG, "No valid HA host in config_store; falling back to compile-time MQTT config");
            }
        }

        if (s_uri.empty())
        {
            s_uri = HA_MQTT_URI;
            s_user = HA_MQTT_USERNAME;
            s_pass = HA_MQTT_PASSWORD;
            s_client_id = HA_MQTT_CLIENT_ID;
            s_host.clear();
            s_port = 0;

            ESP_LOGI(TAG, "Using fallback MQTT config: uri=%s user='%s'", s_uri.c_str(), s_user.c_str());
        }

        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = s_uri.c_str();
        if (!s_user.empty())
            cfg.credentials.username = s_user.c_str();
        if (!s_pass.empty())
            cfg.credentials.authentication.password = s_pass.c_str();
        cfg.session.last_will.topic = HA_MQTT_STATUS_TOPIC;
        cfg.session.last_will.msg = "offline";
        cfg.session.last_will.qos = 1;
        cfg.session.last_will.retain = true;
        cfg.credentials.client_id = s_client_id.c_str();
        cfg.task.priority = 5;
        cfg.buffer.size = 2048;
        cfg.network.reconnect_timeout_ms = 3000;

        s_client = esp_mqtt_client_init(&cfg);
        if (!s_client)
            return ESP_ERR_NO_MEM;
        esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_mqtt_event, nullptr);
        esp_err_t err = esp_mqtt_client_start(s_client);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
            return err;
        }
        return ESP_OK;
    }

    bool is_connected()
    {
        return s_connected;
    }

    esp_err_t publish_toggle(const char *entity_id)
    {
        if (!entity_id || !*entity_id)
            return ESP_ERR_INVALID_ARG;
        if (!s_client)
            return ESP_ERR_INVALID_STATE;
        int msg_id = esp_mqtt_client_publish(s_client, HA_MQTT_CMD_TOGGLE_TOPIC, entity_id, 0, 1, false);
        if (msg_id < 0)
            return ESP_FAIL;
        ESP_LOGI(TAG, "MQTT toggle -> %s (%s)", entity_id, HA_MQTT_CMD_TOGGLE_TOPIC);
        return ESP_OK;
    }

    void set_message_handler(MessageHandler handler)
    {
        s_handler = handler;
    }

    esp_err_t subscribe(const char *topic, int qos)
    {
        if (!topic || !*topic)
            return ESP_ERR_INVALID_ARG;
        if (s_subs_count < (int)(sizeof(s_subs) / sizeof(s_subs[0])))
        {
            std::strncpy(s_subs[s_subs_count].topic, topic, sizeof(s_subs[s_subs_count].topic) - 1);
            s_subs[s_subs_count].topic[sizeof(s_subs[s_subs_count].topic) - 1] = '\0';
            s_subs[s_subs_count].qos = qos;
            ++s_subs_count;
        }
        if (s_client && s_connected)
        {
            int mid = esp_mqtt_client_subscribe(s_client, topic, qos);
            return (mid >= 0) ? ESP_OK : ESP_FAIL;
        }
        return ESP_OK;
    }

} // namespace ha_mqtt
