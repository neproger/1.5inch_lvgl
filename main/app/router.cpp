#include "app/router.hpp"
#include "ha_mqtt.hpp"
#include "app/entities.hpp"
#include "state_manager.hpp"
#include <cstring>
#include <cstdio>
#include <string>
#include "esp_log.h"

namespace
{
    static const char *TAG = "router";

    void on_mqtt_msg(const char *topic, const char *data, int len)
    {
        if (!topic)
            return;

        ESP_LOGI(TAG, "MQTT RX topic='%s' payload='%.*s' (len=%d)",
                 topic,
                 len,
                 (const char *)data,
                 len);

        const char prefix[] = "ha/state/";
        const size_t pfx_len = sizeof(prefix) - 1;
        if (std::strncmp(topic, prefix, pfx_len) != 0)
            return;
        const char *entity = topic + pfx_len;

        if (data && len >= 0)
        {
            std::string value(data, data + len);
            state::set_entity_state(entity, value);
        }
    }

} // namespace

namespace router
{

    esp_err_t start()
    {
        const auto &ents = state::entities();
        bool use_state_entities = !ents.empty();

        // Ограничиваем количество сущностей для подписки, чтобы не выходить
        // за пределы внутренних структур MQTT-клиента.
        constexpr int kMaxTrackedEntities = 8;
        int count = 0;

        if (use_state_entities)
        {
            count = static_cast<int>(ents.size());
        }
        else
        {
            count = app::g_entity_count;
        }
        if (count > kMaxTrackedEntities)
            count = kMaxTrackedEntities;

        esp_err_t err = ha_mqtt::start();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "MQTT start failed: %s", esp_err_to_name(err));
            return err;
        }

        ha_mqtt::set_message_handler(&on_mqtt_msg);

        for (int i = 0; i < count; ++i)
        {
            const char *entity_id = nullptr;
            if (use_state_entities)
            {
                entity_id = ents[static_cast<size_t>(i)].id.c_str();
            }
            else
            {
                entity_id = app::g_entities[i].entity_id;
            }

            char topic[128];
            std::snprintf(topic, sizeof(topic), "ha/state/%s", entity_id);
            ha_mqtt::subscribe(topic, 2);
        }

        return ESP_OK;
    }

    bool is_connected()
    {
        return ha_mqtt::is_connected();
    }

    esp_err_t toggle(const char *entity_id)
    {
        esp_err_t err = ha_mqtt::publish_toggle(entity_id);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to publish toggle: %s", esp_err_to_name(err));
        }
        return err;
    }

} // namespace router

