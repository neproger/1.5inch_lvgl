#pragma once

#include "lvgl.h"
#include "rooms.hpp"

#include <vector>

namespace ui
{
    namespace screensaver
    {
        extern lv_obj_t *s_screensaver_root;
        extern lv_obj_t *s_weather_temp_label;
        extern lv_obj_t *s_weather_cond_label;
        extern lv_obj_t *s_weather_icon;
        extern lv_obj_t *s_time_label;
        extern lv_obj_t *s_date_label;
        extern lv_obj_t *s_week_day_label;

        void ui_build_screensaver();

        // Update weather info on both room pages and screensaver
        void ui_update_weather_and_clock();

        // Start periodic weather HTTP polling and update UI on changes
        void start_weather_polling();

        void show();
        void hide_to_room(lv_obj_t *room_root);
    } // namespace screensaver
} // namespace ui
