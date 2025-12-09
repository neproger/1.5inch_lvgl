#include "input_controller.hpp"

#include "esp_event.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "app_events.hpp"
#include "lvgl.h"
#include "ui/rooms.hpp"
#include "ui/switch.hpp"

namespace input_controller
{

    namespace
    {
        static const char *TAG = "input_controller";

        enum class KnobCode
        {
            Right = KNOB_RIGHT,
            Left = KNOB_LEFT,
            HighLimit = KNOB_H_LIM,
            LowLimit = KNOB_L_LIM,
            Zero = KNOB_ZERO,
        };

        enum class ButtonCode
        {
            SingleClick = BUTTON_SINGLE_CLICK,
        };

        // C entry points from devices_init.c (knob/button callbacks)
        extern "C" void LVGL_knob_event(void *event)
        {
            int ev = (int)(intptr_t)event;
            std::int64_t now_us = esp_timer_get_time();

            // Publish as application-level raw knob event
            (void)app_events::post_knob(ev, now_us, false);

            // Let LVGL know there was user activity (for inactivity timers)
            lv_display_trigger_activity(nullptr);
        }

        extern "C" void LVGL_button_event(void *event)
        {
            int ev = (int)(intptr_t)event;
            std::int64_t now_us = esp_timer_get_time();

            // Publish as application-level raw button event
            (void)app_events::post_button(ev, now_us, false);

            // Let LVGL know there was user activity (for inactivity timers)
            lv_display_trigger_activity(nullptr);
        }

        static void on_knob(void * /*arg*/, esp_event_base_t base, int32_t id, void *event_data)
        {
            if (base != APP_EVENTS || id != app_events::KNOB)
            {
                return;
            }

            const auto *payload = static_cast<const app_events::KnobPayload *>(event_data);
            if (!payload)
            {
                return;
            }

            const int code = payload->code;
            const std::int64_t ts = payload->timestamp_us;

            // Any input should request wake
            (void)app_events::post_request_wake(ts, false);

            switch (static_cast<KnobCode>(code))
            {
            case KnobCode::Right: // next room
                (void)app_events::post_navigate_room(+1, ts, false);
                break;
            case KnobCode::Left: // previous room
                (void)app_events::post_navigate_room(-1, ts, false);
                break;
            default:
                break;
            }
        }

        static void on_button(void * /*arg*/, esp_event_base_t base, int32_t id, void *event_data)
        {
            if (base != APP_EVENTS || id != app_events::BUTTON)
            {
                return;
            }

            const auto *payload = static_cast<const app_events::ButtonPayload *>(event_data);
            if (!payload)
            {
                return;
            }

            const int code = payload->code;
            const std::int64_t ts = payload->timestamp_us;

            // Any input should request wake
            (void)app_events::post_request_wake(ts, false);

            // SINGLE_CLICK toggles current entity
            if (code == static_cast<int>(ButtonCode::SingleClick))
            {
                (void)app_events::post_toggle_current_entity(ts, false);
            }
        }

        static void on_gesture(void * /*arg*/, esp_event_base_t base, int32_t id, void *event_data)
        {
            if (base != APP_EVENTS || id != app_events::GESTURE)
            {
                return;
            }

            const auto *payload = static_cast<const app_events::GesturePayload *>(event_data);
            if (!payload)
            {
                return;
            }

            const int code = payload->code;
            const std::int64_t ts = payload->timestamp_us;

            // Any gesture should request wake
            (void)app_events::post_request_wake(ts, false);

            // SwipeLeft = next room, SwipeRight = previous room
            switch (static_cast<app_events::GestureCode>(code))
            {
            case app_events::GestureCode::SwipeLeft:
                (void)app_events::post_navigate_room(+1, ts, false);
                break;
            case app_events::GestureCode::SwipeRight:
                (void)app_events::post_navigate_room(-1, ts, false);
                break;
            default:
                break;
            }
        }

        static void on_toggle_entity(void * /*arg*/, esp_event_base_t base, int32_t id, void * /*event_data*/)
        {
            if (base != APP_EVENTS || id != app_events::TOGGLE_CURRENT_ENTITY)
            {
                return;
            }

            lvgl_port_lock(-1);

            std::string entity_id;

            if (!ui::rooms::get_current_entity_id(entity_id))
            {
                ESP_LOGW(TAG, "No entity selected for toggle");
                lvgl_port_unlock();
                return;
            }

            std::int64_t now_us = esp_timer_get_time();
            (void)app_events::post_toggle_request(entity_id.c_str(), now_us, false);

            lvgl_port_unlock();
        }

    } // namespace

    esp_err_t init()
    {
        esp_event_handler_instance_t h_knob = nullptr;
        esp_event_handler_instance_t h_button = nullptr;
        esp_event_handler_instance_t h_gesture = nullptr;
        esp_event_handler_instance_t h_toggle = nullptr;

        esp_err_t err = ESP_OK;

        err |= esp_event_handler_instance_register(
            APP_EVENTS,
            app_events::KNOB,
            &on_knob,
            nullptr,
            &h_knob);

        err |= esp_event_handler_instance_register(
            APP_EVENTS,
            app_events::BUTTON,
            &on_button,
            nullptr,
            &h_button);

        err |= esp_event_handler_instance_register(
            APP_EVENTS,
            app_events::GESTURE,
            &on_gesture,
            nullptr,
            &h_gesture);

        err |= esp_event_handler_instance_register(
            APP_EVENTS,
            app_events::TOGGLE_CURRENT_ENTITY,
            &on_toggle_entity,
            nullptr,
            &h_toggle);

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "failed to register one or more handlers: %s", esp_err_to_name(err));
        }

        return err;
    }

} // namespace input_controller
