#include "event_logger.hpp"

#include "esp_event.h"
#include "esp_log.h"
#include "app_events.hpp"

namespace event_logger
{

    namespace
    {
        static const char *TAG = "APP_EVENT_BUS";
        static esp_event_handler_instance_t s_any_instance = nullptr;

        static void log_event(void * /*arg*/,
                              esp_event_base_t event_base,
                              int32_t event_id,
                              void *event_data)
        {
            const char *base_str = event_base ? event_base : "NULL";
            if (event_base == APP_EVENTS)
            {
                switch (event_id)
                {
                case app_events::KNOB:
                {
                    auto *p = static_cast<const app_events::KnobPayload *>(event_data);
                    int code = p ? p->code : -1;
                    ESP_LOGI(TAG,
                             "event: base=%s id=KNOB code=%d",
                             base_str,
                             code);
                    break;
                }
                case app_events::BUTTON:
                {
                    auto *p = static_cast<const app_events::ButtonPayload *>(event_data);
                    int code = p ? p->code : -1;
                    ESP_LOGI(TAG,
                             "event: base=%s id=BUTTON code=%d",
                             base_str,
                             code);
                    break;
                }
                case app_events::GESTURE:
                {
                    auto *p = static_cast<const app_events::GesturePayload *>(event_data);
                    int code = p ? p->code : -1;
                    ESP_LOGI(TAG,
                             "event: base=%s id=GESTURE code=%d",
                             base_str,
                             code);
                    break;
                }
                case app_events::NAVIGATE_ROOM:
                {
                    auto *p = static_cast<const app_events::NavigateRoomPayload *>(event_data);
                    int delta = p ? p->delta : 0;
                    ESP_LOGI(TAG,
                             "event: base=%s id=NAVIGATE_ROOM delta=%d",
                             base_str,
                             delta);
                    break;
                }
                case app_events::WAKE_SCREENSAVER:
                    ESP_LOGI(TAG,
                             "event: base=%s id=WAKE_SCREENSAVER",
                             base_str);
                    break;
                case app_events::TOGGLE_CURRENT_ENTITY:
                    ESP_LOGI(TAG,
                             "event: base=%s id=TOGGLE_CURRENT_ENTITY",
                             base_str);
                    break;
                case app_events::ENTITY_STATE_CHANGED:
                {
                    auto *p = static_cast<const app_events::EntityStateChangedPayload *>(event_data);
                    const char *id_str = (p && p->entity_id[0]) ? p->entity_id : "<null>";
                    ESP_LOGI(TAG,
                             "event: base=%s id=ENTITY_STATE_CHANGED entity_id=%s",
                             base_str,
                             id_str);
                    break;
                }
                case app_events::TOGGLE_REQUEST:
                {
                    auto *p = static_cast<const app_events::ToggleRequestPayload *>(event_data);
                    const char *id_str = (p && p->entity_id[0]) ? p->entity_id : "<null>";
                    ESP_LOGI(TAG,
                             "event: base=%s id=TOGGLE_REQUEST entity_id=%s",
                             base_str,
                             id_str);
                    break;
                }
                case app_events::TOGGLE_RESULT:
                {
                    auto *p = static_cast<const app_events::ToggleResultPayload *>(event_data);
                    const char *id_str = (p && p->entity_id[0]) ? p->entity_id : "<null>";
                    bool ok = p ? p->success : false;
                    ESP_LOGI(TAG,
                             "event: base=%s id=TOGGLE_RESULT entity_id=%s success=%d",
                             base_str,
                             id_str,
                             (int)ok);
                    break;
                }
                default:
                    ESP_LOGI(TAG,
                             "event: base=%s id=%ld (APP_EVENTS unknown)",
                             base_str,
                             static_cast<long>(event_id));
                    break;
                }
            }
            else
            {
                ESP_LOGI(TAG, "event: base=%s id=%ld",
                         base_str,
                         static_cast<long>(event_id));
            }
        }
    } // namespace

    esp_err_t init()
    {
        // Subscribe only to application-level events on the default loop
        esp_err_t err = esp_event_handler_instance_register(
            APP_EVENTS,
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
