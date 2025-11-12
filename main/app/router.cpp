#include "app/router.hpp"
#include "ha_mqtt.hpp"
#include "core/store.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
TaskHandle_t s_mon_task = nullptr;
bool s_last_conn = false;

void monitor_task(void*)
{
    for (;;) {
        bool c = ha_mqtt::is_connected();
        if (c != s_last_conn) {
            s_last_conn = c;
            core::store_dispatch_connected(c);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
}

namespace router {

esp_err_t start()
{
    // For now delegate to MQTT; later can wire Store/Services here.
    (void)core::store_start();
    esp_err_t err = ha_mqtt::start();
    if (err != ESP_OK) return err;
    if (!s_mon_task) {
        xTaskCreate(monitor_task, "router_mon", 2048, nullptr, 3, &s_mon_task);
    }
    return ESP_OK;
}

bool is_connected()
{
    return ha_mqtt::is_connected();
}

esp_err_t toggle(const char* entity_id)
{
    return ha_mqtt::publish_toggle(entity_id);
}

} // namespace router
