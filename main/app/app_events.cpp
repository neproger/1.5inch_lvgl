#include "app_events.hpp"

#include "esp_log.h"

ESP_EVENT_DEFINE_BASE(APP_EVENTS);

namespace app_events
{

    namespace
    {
        static const char *TAG = "app_events";
    }

    esp_err_t post_knob(int code, std::int64_t timestamp_us, bool from_isr)
    {
        KnobPayload payload;
        payload.code = code;
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     KNOB,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 KNOB,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_knob failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t post_button(int code, std::int64_t timestamp_us, bool from_isr)
    {
        ButtonPayload payload;
        payload.code = code;
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     BUTTON,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 BUTTON,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_button failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t post_gesture(int code, std::int64_t timestamp_us, bool from_isr)
    {
        GesturePayload payload;
        payload.code = code;
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     GESTURE,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 GESTURE,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_gesture failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t post_navigate_room(int delta, std::int64_t timestamp_us, bool from_isr)
    {
        NavigateRoomPayload payload;
        payload.delta = delta;
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     NAVIGATE_ROOM,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 NAVIGATE_ROOM,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_navigate_room failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t post_wake_screensaver(std::int64_t timestamp_us, bool from_isr)
    {
        WakeScreensaverPayload payload;
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     WAKE_SCREENSAVER,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 WAKE_SCREENSAVER,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_wake_screensaver failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t post_toggle_current_entity(std::int64_t timestamp_us, bool from_isr)
    {
        ToggleCurrentEntityPayload payload;
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     TOGGLE_CURRENT_ENTITY,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 TOGGLE_CURRENT_ENTITY,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_toggle_current_entity failed: %s", esp_err_to_name(err));
        }
        return err;
    }

} // namespace app_events
