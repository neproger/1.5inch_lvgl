#pragma once

#include "lvgl.h"
#include "esp_err.h"

#include <string>

namespace state
{
    struct Entity;
}

namespace ui
{
    namespace controls
    {
        // Set logical on/off state for a LVGL switch-like control
        void set_switch_state(lv_obj_t *control, bool is_on);

        // Enable or disable user interaction for a LVGL switch-like control
        void set_switch_enabled(lv_obj_t *control, bool enabled);

        // Build a labeled switch widget inside parent and return created objects
        void ui_add_switch_widget(
            lv_obj_t *parent,
            const state::Entity &ent,
            lv_obj_t *&out_label,
            lv_obj_t *&out_control,
            lv_obj_t *&out_ring);
    } // namespace controls

    namespace toggle
    {
        // Initialize toggle handling (registers event bus handlers)
        esp_err_t init();

        // Handle LVGL switch event and trigger toggle over HA
        void switch_event_cb(lv_event_t *e);

        // Trigger toggle for specific entity_id (with debouncing and connectivity checks)
        void trigger_toggle_for_entity(const std::string &entity_id);
    } // namespace toggle
} // namespace ui
