#pragma once

#include "esp_event.h"
#include <cstdint>

// Application-level event base for internal messages
ESP_EVENT_DECLARE_BASE(APP_EVENTS);

namespace app_events
{

    enum Id : int32_t
    {
        KNOB = 1,
        BUTTON = 2,
        GESTURE = 3,
        NAVIGATE_ROOM = 10,
        WAKE_SCREENSAVER = 11,
        TOGGLE_CURRENT_ENTITY = 12,
        ENTITY_STATE_CHANGED = 20,
        TOGGLE_REQUEST = 30,
        TOGGLE_RESULT = 31,
    };

    enum class GestureCode : int
    {
        SwipeLeft = 0,
        SwipeRight = 1,
    };

    struct KnobPayload
    {
        int code = 0;
        std::int64_t timestamp_us = 0;
    };

    struct ButtonPayload
    {
        int code = 0;
        std::int64_t timestamp_us = 0;
    };

    struct GesturePayload
    {
        int code = 0;
        std::int64_t timestamp_us = 0;
    };

    struct NavigateRoomPayload
    {
        int delta = 0;
        std::int64_t timestamp_us = 0;
    };

    struct WakeScreensaverPayload
    {
        std::int64_t timestamp_us = 0;
    };

    struct ToggleCurrentEntityPayload
    {
        std::int64_t timestamp_us = 0;
    };

    struct EntityStateChangedPayload
    {
        char entity_id[96];
        std::int64_t timestamp_us = 0;
    };

    struct ToggleRequestPayload
    {
        char entity_id[96];
        std::int64_t timestamp_us = 0;
    };

    struct ToggleResultPayload
    {
        char entity_id[96];
        bool success = false;
        std::int64_t timestamp_us = 0;
    };

    // Currently a no-op stub; kept for symmetry / future use
    inline esp_err_t init()
    {
        return ESP_OK;
    }

    esp_err_t post_knob(int code, std::int64_t timestamp_us, bool from_isr);
    esp_err_t post_button(int code, std::int64_t timestamp_us, bool from_isr);
    esp_err_t post_gesture(int code, std::int64_t timestamp_us, bool from_isr);
    esp_err_t post_navigate_room(int delta, std::int64_t timestamp_us, bool from_isr);
    esp_err_t post_wake_screensaver(std::int64_t timestamp_us, bool from_isr);
    esp_err_t post_toggle_current_entity(std::int64_t timestamp_us, bool from_isr);
    esp_err_t post_entity_state_changed(const char *entity_id, std::int64_t timestamp_us, bool from_isr);
    esp_err_t post_toggle_request(const char *entity_id, std::int64_t timestamp_us, bool from_isr);
    esp_err_t post_toggle_result(const char *entity_id, bool success, std::int64_t timestamp_us, bool from_isr);

} // namespace app_events
