#pragma once

#include "lvgl.h"

#include <string>
#include <vector>

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
    } // namespace rooms
} // namespace ui
