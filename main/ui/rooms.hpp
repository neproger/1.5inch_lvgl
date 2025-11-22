#pragma once

#include "lvgl.h"

#include <string>
#include <vector>

namespace state
{
    struct Entity;
}

namespace ui
{
    namespace rooms
    {
        struct DeviceWidget
        {
            std::string entity_id;
            std::string name;
            lv_obj_t *container = nullptr;
            lv_obj_t *label = nullptr;
            lv_obj_t *control = nullptr; // e.g. lv_switch
        };

        struct RoomPage
        {
            std::string area_id;
            std::string area_name;
            lv_obj_t *root = nullptr;
            lv_obj_t *title_label = nullptr;
            lv_obj_t *list_container = nullptr;
            std::vector<DeviceWidget> devices;
        };

        extern std::vector<RoomPage> s_room_pages;
        extern int s_current_room_index;
        extern int s_current_device_index;

        void ui_build_room_pages();

        // Show initial room screen (index 0) if any rooms exist
        void show_initial_room();

        // Move to room by relative offset (with wrap-around) and load it with animation
        void show_room_relative(int delta, lv_screen_load_anim_t anim_type);

        // Get entity_id of the currently selected device in the current room
        bool get_current_entity_id(std::string &out_entity_id);

        // Find entity_id for a given LVGL control (switch) on any room page
        bool find_entity_for_control(lv_obj_t *control, std::string &out_entity_id);

        // Apply updated entity state to corresponding widgets on room pages
        void on_entity_state_changed(const state::Entity &e);

    } // namespace rooms
} // namespace ui
