#include "app/router.hpp"
#include "ha_mqtt.hpp"
#include "core/store.hpp"
#include "app/entities.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <cstdio>
#include "esp_log.h"

namespace
{
    TaskHandle_t s_mon_task = nullptr;
    bool s_last_conn = false;

    void on_mqtt_msg(const char *topic, const char *data, int len)
    {
        if (!topic)
            return;
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
        int count = app::g_entity_count;
        if (count > core::kMaxEntities)
            count = core::kMaxEntities;
        for (int i = 0; i < count; ++i)
            ids[i] = app::g_entities[i].entity_id;
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
            snprintf(topic, sizeof(topic), "ha/state/%s", app::g_entities[i].entity_id);
            ha_mqtt::subscribe(topic, 1);
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
