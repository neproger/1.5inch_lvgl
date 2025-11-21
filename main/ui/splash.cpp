#include "splash.hpp"

#include "esp_lvgl_port.h"
#include "fonts.h"
#include "icons.h"

namespace ui
{
    namespace splash
    {
        lv_obj_t *s_splash_root = nullptr;
        lv_obj_t *s_splash_bar = nullptr;

        void show()
        {
            if (s_splash_root)
            {
                lv_disp_load_scr(s_splash_root);
                return;
            }

            s_splash_root = lv_obj_create(NULL);
            lv_obj_set_size(s_splash_root, LV_HOR_RES, LV_VER_RES);
            lv_obj_set_style_bg_color(s_splash_root, lv_color_hex(0x000000), 0);
            lv_obj_set_style_border_width(s_splash_root, 0, 0);
            lv_obj_remove_flag(s_splash_root, LV_OBJ_FLAG_SCROLLABLE);

            lv_obj_t *icon = lv_image_create(s_splash_root);
            lv_image_set_src(icon, &alien);
            lv_obj_set_style_img_recolor(icon, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, 0);
            lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);

            s_splash_bar = lv_bar_create(s_splash_root);
            lv_obj_set_width(s_splash_bar, LV_PCT(50));
            lv_obj_set_height(s_splash_bar, 8);
            lv_obj_set_style_bg_color(s_splash_bar, lv_color_hex(0x303030), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(s_splash_bar, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_bg_color(s_splash_bar, lv_color_hex(0xE6E6E6), LV_PART_INDICATOR);
            lv_obj_set_style_bg_opa(s_splash_bar, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_align(s_splash_bar, LV_ALIGN_BOTTOM_MID, 0, -70);
            lv_bar_set_range(s_splash_bar, 0, 100);
            lv_bar_set_value(s_splash_bar, 0, LV_ANIM_OFF);

            lv_disp_load_scr(s_splash_root);
            lv_refr_now(NULL);
        }

        void update_progress(int percent)
        {
            if (!s_splash_bar)
            {
                return;
            }
            if (percent < 0)
                percent = 0;
            if (percent > 100)
                percent = 100;
            lv_bar_set_value(s_splash_bar, percent, LV_ANIM_OFF);
        }

        void destroy()
        {
            if (!s_splash_root)
            {
                return;
            }
            if (lv_screen_active() == s_splash_root)
            {
                return;
            }
            lv_obj_del(s_splash_root);
            s_splash_root = nullptr;
            s_splash_bar = nullptr;
        }
    } // namespace splash
} // namespace ui

