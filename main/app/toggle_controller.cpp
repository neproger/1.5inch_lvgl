#include "toggle_controller.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_events.hpp"
#include "app/router.hpp"
#include "wifi_manager.h"

#include <cstring>

namespace toggle_controller
{

    namespace
    {
        static const char *TAG = "toggle_controller";
        static TaskHandle_t s_toggle_task = nullptr;

        static void ha_toggle_task(void *arg)
        {
            char *entity = static_cast<char *>(arg);

            bool success = false;

            if (!wifi_manager_is_connected())
            {
                ESP_LOGW(TAG, "No WiFi, cannot toggle entity");
            }
            else
            {
                esp_err_t err = router::toggle(entity);
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG, "MQTT toggle error: %d", (int)err);
                }
                else
                {
                    success = true;
                }
            }

            std::int64_t now_us = esp_timer_get_time();
            (void)app_events::post_toggle_result(entity ? entity : "", success, now_us, false);

            if (entity)
                std::free(entity);

            s_toggle_task = nullptr;
            vTaskDelete(nullptr);
        }

        static void on_toggle_request(void * /*arg*/, esp_event_base_t base, int32_t id, void *event_data)
        {
            if (base != APP_EVENTS || id != app_events::TOGGLE_REQUEST)
            {
                return;
            }

            const auto *payload = static_cast<const app_events::ToggleRequestPayload *>(event_data);
            if (!payload || !payload->entity_id[0])
            {
                return;
            }

            if (s_toggle_task != nullptr)
            {
                ESP_LOGW(TAG, "Toggle already in progress, ignoring request for '%s'", payload->entity_id);
                return;
            }

            char *arg = static_cast<char *>(std::malloc(std::strlen(payload->entity_id) + 1));
            if (!arg)
            {
                ESP_LOGE(TAG, "No memory for toggle arg");
                std::int64_t now_us = esp_timer_get_time();
                (void)app_events::post_toggle_result(payload->entity_id, false, now_us, false);
                return;
            }
            std::memcpy(arg, payload->entity_id, std::strlen(payload->entity_id) + 1);

            BaseType_t ok = xTaskCreate(ha_toggle_task, "ha_toggle", 4096, arg, 4, &s_toggle_task);
            if (ok != pdPASS)
            {
                std::free(arg);
                s_toggle_task = nullptr;
                ESP_LOGW(TAG, "Failed to create ha_toggle task");
                std::int64_t now_us = esp_timer_get_time();
                (void)app_events::post_toggle_result(payload->entity_id, false, now_us, false);
            }
        }

    } // namespace

    esp_err_t init()
    {
        static bool s_registered = false;
        if (s_registered)
        {
            return ESP_OK;
        }

        esp_event_handler_instance_t inst = nullptr;
        esp_err_t err = esp_event_handler_instance_register(
            APP_EVENTS,
            app_events::TOGGLE_REQUEST,
            &on_toggle_request,
            nullptr,
            &inst);

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "failed to register TOGGLE_REQUEST handler: %s", esp_err_to_name(err));
        }
        else
        {
            s_registered = true;
        }

        return err;
    }

} // namespace toggle_controller
