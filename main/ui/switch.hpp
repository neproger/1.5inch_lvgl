#pragma once

#include "lvgl.h"

#include <string>

namespace ui
{
    namespace controls
    {
        // Set logical on/off state for a LVGL switch-like control
        void set_switch_state(lv_obj_t *control, bool is_on);

        // Enable or disable user interaction for a LVGL switch-like control
        void set_switch_enabled(lv_obj_t *control, bool enabled);
    } // namespace controls

    namespace toggle
    {
        // Handle LVGL switch event and trigger toggle over HA
        void switch_event_cb(lv_event_t *e);

        // Trigger toggle for specific entity_id (with debouncing and connectivity checks)
        void trigger_toggle_for_entity(const std::string &entity_id);
    } // namespace toggle
} // namespace ui
