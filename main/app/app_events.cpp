#include "app_events.hpp"

#include "esp_log.h"
#include <cstdio>

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

    esp_err_t post_entity_state_changed(const char *entity_id, std::int64_t timestamp_us, bool from_isr)
    {
        if (!entity_id || !*entity_id)
        {
            return ESP_ERR_INVALID_ARG;
        }

        EntityStateChangedPayload payload{};
        std::snprintf(payload.entity_id, sizeof(payload.entity_id), "%s", entity_id);
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     ENTITY_STATE_CHANGED,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 ENTITY_STATE_CHANGED,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_entity_state_changed failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t post_toggle_request(const char *entity_id, std::int64_t timestamp_us, bool from_isr)
    {
        if (!entity_id || !*entity_id)
        {
            return ESP_ERR_INVALID_ARG;
        }

        ToggleRequestPayload payload{};
        std::snprintf(payload.entity_id, sizeof(payload.entity_id), "%s", entity_id);
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     TOGGLE_REQUEST,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 TOGGLE_REQUEST,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_toggle_request failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t post_toggle_result(const char *entity_id, bool success, std::int64_t timestamp_us, bool from_isr)
    {
        if (!entity_id || !*entity_id)
        {
            return ESP_ERR_INVALID_ARG;
        }

        ToggleResultPayload payload{};
        std::snprintf(payload.entity_id, sizeof(payload.entity_id), "%s", entity_id);
        payload.success = success;
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     TOGGLE_RESULT,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 TOGGLE_RESULT,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_toggle_result failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t post_app_state_changed(AppState old_state, AppState new_state, std::int64_t timestamp_us, bool from_isr)
    {
        AppStateChangedPayload payload{};
        payload.old_state = static_cast<int>(old_state);
        payload.new_state = static_cast<int>(new_state);
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     APP_STATE_CHANGED,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 APP_STATE_CHANGED,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_app_state_changed failed: %s", esp_err_to_name(err));
        }
        return err;
    }

    static esp_err_t post_empty_event(int32_t id, std::int64_t timestamp_us, bool from_isr)
    {
        EmptyPayload payload{};
        payload.timestamp_us = timestamp_us;

        esp_err_t err;
        if (from_isr)
        {
            err = esp_event_isr_post(APP_EVENTS,
                                     id,
                                     &payload,
                                     sizeof(payload),
                                     nullptr);
        }
        else
        {
            err = esp_event_post(APP_EVENTS,
                                 id,
                                 &payload,
                                 sizeof(payload),
                                 0);
        }

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "post_empty_event(id=%d) failed: %s", (int)id, esp_err_to_name(err));
        }
        return err;
    }

    esp_err_t post_request_config_mode(std::int64_t timestamp_us, bool from_isr)
    {
        return post_empty_event(REQUEST_CONFIG_MODE, timestamp_us, from_isr);
    }

    esp_err_t post_request_sleep(std::int64_t timestamp_us, bool from_isr)
    {
        return post_empty_event(REQUEST_SLEEP, timestamp_us, from_isr);
    }

    esp_err_t post_request_wake(std::int64_t timestamp_us, bool from_isr)
    {
        return post_empty_event(REQUEST_WAKE, timestamp_us, from_isr);
    }

    const char *id_to_string(int32_t id)
    {
        switch (id)
        {
        case KNOB:
            return "KNOB";
        case BUTTON:
            return "BUTTON";
        case GESTURE:
            return "GESTURE";
        case NAVIGATE_ROOM:
            return "NAVIGATE_ROOM";
        case TOGGLE_CURRENT_ENTITY:
            return "TOGGLE_CURRENT_ENTITY";
        case ENTITY_STATE_CHANGED:
            return "ENTITY_STATE_CHANGED";
        case WEATHER_UPDATED:
            return "WEATHER_UPDATED";
        case CLOCK_UPDATED:
            return "CLOCK_UPDATED";
        case TOGGLE_REQUEST:
            return "TOGGLE_REQUEST";
        case TOGGLE_RESULT:
            return "TOGGLE_RESULT";
        case APP_STATE_CHANGED:
            return "APP_STATE_CHANGED";
        case REQUEST_CONFIG_MODE:
            return "REQUEST_CONFIG_MODE";
        case REQUEST_SLEEP:
            return "REQUEST_SLEEP";
        case REQUEST_WAKE:
            return "REQUEST_WAKE";
        default:
            return "UNKNOWN";
        }
    }

    esp_err_t post_weather_updated(std::int64_t timestamp_us, bool from_isr)
    {
        return post_empty_event(WEATHER_UPDATED, timestamp_us, from_isr);
    }

    esp_err_t post_clock_updated(std::int64_t timestamp_us, bool from_isr)
    {
        return post_empty_event(CLOCK_UPDATED, timestamp_us, from_isr);
    }

} // namespace app_events
