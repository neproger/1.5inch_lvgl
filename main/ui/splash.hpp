#pragma once

#include "lvgl.h"

namespace ui
{
    namespace splash
    {
        extern lv_obj_t *s_splash_root;
        extern lv_obj_t *s_splash_bar;

        void show();
        void update_progress(int percent);
        void destroy();
    } // namespace splash
} // namespace ui

