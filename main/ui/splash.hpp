#pragma once

#include "lvgl.h"

namespace ui
{
    namespace splash
    {
        extern lv_obj_t *s_splash_root;
        extern lv_obj_t *s_splash_bar;
        extern lv_obj_t *s_splash_label;

        void show();
        void update_progress(int percent);
        void update_state(int percent, const char *text);
        void destroy();
    } // namespace splash
} // namespace ui
