#include "event_logger.hpp"

#include "esp_event.h"
#include "esp_log.h"

namespace event_logger
{

    namespace
    {
        static const char *TAG = "event_logger";
        static esp_event_handler_instance_t s_any_instance = nullptr;

        static void log_event(void * /*arg*/,
                              esp_event_base_t event_base,
                              int32_t event_id,
                              void * /*event_data*/)
        {
            const char *base_str = event_base ? event_base : "NULL";
            ESP_LOGI(TAG, "event: base=%s id=%ld",
                     base_str,
                     static_cast<long>(event_id));
        }
    } // namespace

    esp_err_t init()
    {
        // Subscribe to all events on the default loop
        esp_err_t err = esp_event_handler_instance_register(
            ESP_EVENT_ANY_BASE,
            ESP_EVENT_ANY_ID,
            &log_event,
            nullptr,
            &s_any_instance);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "failed to register event logger: %s", esp_err_to_name(err));
        }

        return err;
    }

} // namespace event_logger

