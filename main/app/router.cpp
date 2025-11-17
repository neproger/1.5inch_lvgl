#include "app/router.hpp"
#include "ha_mqtt.hpp"
#include "core/store.hpp"
#include "app/entities.hpp"
#include "state_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>
#include "esp_log.h"

namespace
{
    static const char *TAG = "router";
    TaskHandle_t s_mon_task = nullptr;
    bool s_last_conn = false;

    void on_mqtt_msg(const char *topic, const char *data, int len)
    {
        if (!topic)
            return;

        // 1. Логируем вообще всё, что прилетает с брокера
        // data может быть бинарным и не нуль-терминированным, поэтому %.*s
        ESP_LOGI(TAG, "MQTT RX topic='%s' payload='%.*s' (len=%d)",
                 topic,
                 len,
                 (const char *)data,
                 len);

        const char prefix[] = "ha/state/";
        const size_t pfx_len = sizeof(prefix) - 1;
        if (strncmp(topic, prefix, pfx_len) != 0)
            return;
        const char *entity = topic + pfx_len;
        core::store_dispatch_entity_state(entity, data, len);
    }

    void monitor_task(void *)
    {
        for (;;)
        {
            bool c = ha_mqtt::is_connected();
            if (c != s_last_conn)
            {
                s_last_conn = c;
                core::store_dispatch_connected(c);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

namespace router
{

    esp_err_t start()
    {

        // For now delegate to MQTT; later can wire Store/Services here.
        (void)core::store_start();
        const char *ids[core::kMaxEntities];
        int count = 0;

        const auto &ents = state::entities();
        bool use_state_entities = !ents.empty();

        if (use_state_entities)
        {
            int max = static_cast<int>(ents.size());
            if (max > core::kMaxEntities)
                max = core::kMaxEntities;
            for (int i = 0; i < max; ++i)
            {
                ids[i] = ents[static_cast<size_t>(i)].id.c_str();
            }
            count = max;
        }
        else
        {
            int max = app::g_entity_count;
            if (max > core::kMaxEntities)
                max = core::kMaxEntities;
            for (int i = 0; i < max; ++i)
            {
                ids[i] = app::g_entities[i].entity_id;
            }
            count = max;
        }

        core::store_init_entities(ids, count);
        esp_err_t err = ha_mqtt::start();
        if (err != ESP_OK)
        {
            ESP_LOGE("app", "MQTT start failed: %s", esp_err_to_name(err));
        }
        ha_mqtt::set_message_handler(&on_mqtt_msg);
        for (int i = 0; i < count; ++i)
        {
            char topic[128];
            const char *entity_id = nullptr;
            if (use_state_entities)
            {
                entity_id = ents[static_cast<size_t>(i)].id.c_str();
            }
            else
            {
                entity_id = app::g_entities[i].entity_id;
            }
            snprintf(topic, sizeof(topic), "ha/state/%s", entity_id);
            ha_mqtt::subscribe(topic, 2);
        }
        s_last_conn = ha_mqtt::is_connected();
        core::store_dispatch_connected(s_last_conn);
        if (!s_mon_task)
        {
            xTaskCreate(monitor_task, "router_mon", 2048, nullptr, 3, &s_mon_task);
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
            ESP_LOGE("app", "Failed to publish toggle: %s", esp_err_to_name(err));
        }
        return err;
    }

} // namespace router
