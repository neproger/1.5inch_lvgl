#include <cstring>
#include "esp_log.h"
#include "esp_event.h"
#include "mqtt_client.h"

#include "ha_mqtt.hpp"
#include "config/config.hpp"

namespace ha_mqtt
{

    static const char *TAG = "ha_mqtt";
    static esp_mqtt_client_handle_t s_client = nullptr;
    static volatile bool s_connected = false;
    static MessageHandler s_handler = nullptr;
    static ConnectionHandler s_conn_handler = nullptr;

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
        const auto& cfg = config::ha();
        int len = (int)std::strlen(status);
        (void)esp_mqtt_client_publish(s_client, cfg.status_topic, status, len, 1, true);
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
            if (s_conn_handler) {
                s_conn_handler(true);
            }
            // re-subscribe
            for (int i = 0; i < s_subs_count; ++i) {
                int mid = esp_mqtt_client_subscribe(s_client, s_subs[i].topic, s_subs[i].qos);
                ESP_LOGI(TAG, "SUB %s qos=%d mid=%d", s_subs[i].topic, s_subs[i].qos, mid);
            }
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "SUBACK mid=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "UNSUBACK mid=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "PUBACK mid=%d", event->msg_id);
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "Disconnected from broker");
            if (s_conn_handler) {
                s_conn_handler(false);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT error");
            if (event && event->error_handle) {
                ESP_LOGW(TAG, "err_type=%d tls_last=%d stack=%d sock=%d", 
                         event->error_handle->error_type,
                         event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_tls_stack_err,
                         event->error_handle->esp_transport_sock_errno);
            }
            break;
        case MQTT_EVENT_DATA:
            if (s_handler && event->topic && event->topic_len > 0) {
                // esp-mqtt provides topic with explicit length, not null-terminated.
                // Our handler contract expects a null-terminated topic string.
                size_t tlen = static_cast<size_t>(event->topic_len);
                // Cap to a reasonable stack buffer size
                char topic_buf[192];
                if (tlen >= sizeof(topic_buf)) tlen = sizeof(topic_buf) - 1;
                memcpy(topic_buf, event->topic, tlen);
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

        esp_err_t err = config::load();
        if (err != ESP_OK)
            return err;

        const auto& ha_cfg = config::ha();
        ESP_LOGI(TAG, "MQTT cfg: uri=%s client_id=%s user=%s base=%s",
                 ha_cfg.broker_uri[0] ? ha_cfg.broker_uri : "<none>",
                 ha_cfg.client_id[0] ? ha_cfg.client_id : "<auto>",
                 ha_cfg.username[0] ? ha_cfg.username : "<none>",
                 ha_cfg.base_topic[0] ? ha_cfg.base_topic : "<none>");

        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = ha_cfg.broker_uri;
        // Keepalive shorter to avoid idle disconnects and detect link issues faster
        cfg.session.keepalive = 30;
        // Bump MQTT task resources to avoid starvation under UI load
        cfg.task.priority = 8;
        cfg.task.stack_size = 6144;
        if (ha_cfg.username[0] != '\0')
            cfg.credentials.username = ha_cfg.username;
        if (ha_cfg.password[0] != '\0')
            cfg.credentials.authentication.password = ha_cfg.password;
        cfg.session.last_will.topic = ha_cfg.status_topic;
        cfg.session.last_will.msg = "offline";
        cfg.session.last_will.qos = 1;
        cfg.session.last_will.retain = true;
        if (ha_cfg.client_id[0] != '\0')
            cfg.credentials.client_id = ha_cfg.client_id;
        cfg.buffer.size = 2048;
        cfg.network.reconnect_timeout_ms = 3000;

        s_client = esp_mqtt_client_init(&cfg);
        if (!s_client)
            return ESP_ERR_NO_MEM;
        esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, on_mqtt_event, nullptr);
        err = esp_mqtt_client_start(s_client);
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

    esp_err_t publish(const char *topic, const char *payload, int qos, bool retain)
    {
        if (!topic || !*topic)
            return ESP_ERR_INVALID_ARG;
        if (!s_client)
            return ESP_ERR_INVALID_STATE;
        int len = 0;
        if (payload) {
            len = std::strlen(payload);
        }
        int msg_id = esp_mqtt_client_publish(s_client, topic, payload, len, qos, retain);
        ESP_LOGI(TAG, "PUB %s qos=%d retain=%d mid=%d len=%d",
                 topic, qos, retain ? 1 : 0, msg_id, len);
        return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
    }

    esp_err_t publish_toggle(const char *entity_id)
    {
        if (!entity_id || !*entity_id)
            return ESP_ERR_INVALID_ARG;
        const auto& cfg = config::ha();
        esp_err_t err = publish(cfg.toggle_topic, entity_id, 1, false);
        if (err != ESP_OK)
            return err;
        ESP_LOGI(TAG, "MQTT toggle -> %s (%s)", entity_id, cfg.toggle_topic);
        return ESP_OK;
    }

    void set_message_handler(MessageHandler handler)
    {
        s_handler = handler;
    }

    void set_connection_handler(ConnectionHandler handler)
    {
        s_conn_handler = handler;
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
