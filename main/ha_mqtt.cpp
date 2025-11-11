#include <cstring>
#include <cstdio>
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

#include "ha_mqtt.hpp"
#include "ha_mqtt_config.h"

namespace ha_mqtt
{

    static const char *TAG = "ha_mqtt";
    static esp_mqtt_client_handle_t s_client = nullptr;
    static volatile bool s_connected = false;
    static MessageHandler s_handler = nullptr;

    // Simple subscription registry (re-subscribed on reconnect)
    struct sub_item_t { char topic[96]; int qos; };
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
            // re-subscribe
            for (int i = 0; i < s_subs_count; ++i) {
                int mid = esp_mqtt_client_subscribe(s_client, s_subs[i].topic, s_subs[i].qos);
                ESP_LOGI(TAG, "SUB %s qos=%d mid=%d", s_subs[i].topic, s_subs[i].qos, mid);
            }
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "Disconnected from broker");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT error");
            break;
        case MQTT_EVENT_DATA:
            if (s_handler && event->topic && event->topic_len > 0) {
                s_handler(event->topic, event->data, event->data_len);
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

        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = HA_MQTT_URI;
        if (std::strlen(HA_MQTT_USERNAME) > 0)
            cfg.credentials.username = HA_MQTT_USERNAME;
        if (std::strlen(HA_MQTT_PASSWORD) > 0)
            cfg.credentials.authentication.password = HA_MQTT_PASSWORD;
        cfg.session.last_will.topic = HA_MQTT_STATUS_TOPIC;
        cfg.session.last_will.msg = "offline";
        cfg.session.last_will.qos = 1;
        cfg.session.last_will.retain = true;
        cfg.credentials.client_id = HA_MQTT_CLIENT_ID;
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
        if (!topic || !*topic) return ESP_ERR_INVALID_ARG;
        if (s_subs_count < (int)(sizeof(s_subs)/sizeof(s_subs[0]))) {
            std::strncpy(s_subs[s_subs_count].topic, topic, sizeof(s_subs[s_subs_count].topic) - 1);
            s_subs[s_subs_count].topic[sizeof(s_subs[s_subs_count].topic) - 1] = '\0';
            s_subs[s_subs_count].qos = qos;
            ++s_subs_count;
        }
        if (s_client && s_connected) {
            int mid = esp_mqtt_client_subscribe(s_client, topic, qos);
            return (mid >= 0) ? ESP_OK : ESP_FAIL;
        }
        return ESP_OK;
    }

} // namespace ha_mqtt
