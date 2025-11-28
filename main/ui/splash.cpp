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
        lv_obj_t *s_splash_label = nullptr;
        static lv_obj_t *s_config_button = nullptr;
        static ConfigCallback s_on_config = nullptr;

        static void config_button_event_cb(lv_event_t *e)
        {
            if (!e)
            {
                return;
            }
            if (lv_event_get_code(e) == LV_EVENT_CLICKED)
            {
                if (s_on_config)
                {
                    // Call user callback without LVGL lock held
                    s_on_config();
                }
            }
        }

        void show(ConfigCallback on_config)
        {
            lvgl_port_lock(0);

            s_on_config = on_config;

            if (s_splash_root)
            {
                lv_disp_load_scr(s_splash_root);
                lv_refr_now(NULL);
                lvgl_port_unlock();
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

            s_splash_label = lv_label_create(s_splash_root);
            lv_label_set_text(s_splash_label, "");
            lv_obj_set_style_text_color(s_splash_label, lv_color_hex(0xE6E6E6), 0);
            lv_obj_set_style_text_font(s_splash_label, &Montserrat_20, 0);
            lv_obj_set_style_text_align(s_splash_label, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_align_to(s_splash_label, s_splash_bar, LV_ALIGN_OUT_TOP_LEFT, 0, 0);

            s_config_button = lv_btn_create(s_splash_root);
            lv_obj_set_size(s_config_button, 140, 36);
            lv_obj_align(s_config_button, LV_ALIGN_BOTTOM_MID, 0, -20);
            lv_obj_t *btn_label = lv_label_create(s_config_button);
            lv_obj_set_style_text_font(btn_label, &Montserrat_20, 0);
            lv_label_set_text(btn_label, "Настроить");
            lv_obj_center(btn_label);
            lv_obj_add_event_cb(s_config_button, config_button_event_cb, LV_EVENT_CLICKED, nullptr);

            lv_disp_load_scr(s_splash_root);
            lv_refr_now(NULL);

            lvgl_port_unlock();
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
            lvgl_port_lock(0);
            lv_bar_set_value(s_splash_bar, percent, LV_ANIM_OFF);
            lvgl_port_unlock();
        }

        void update_state(int percent, const char *text)
        {
            lvgl_port_lock(0);
            if (!s_splash_bar)
            {
                lvgl_port_unlock();
                return;
            }
            if (percent < 0)
                percent = 0;
            if (percent > 100)
                percent = 100;
            lv_bar_set_value(s_splash_bar, percent, LV_ANIM_OFF);
            if (s_splash_label && text)
            {
                lv_label_set_text(s_splash_label, text);
            }
            lvgl_port_unlock();
        }

        void destroy()
        {
            lvgl_port_lock(0);

            if (!s_splash_root)
            {
                lvgl_port_unlock();
                return;
            }
            if (lv_screen_active() == s_splash_root)
            {
                lvgl_port_unlock();
                return;
            }
            lv_obj_del(s_splash_root);
            s_splash_root = nullptr;
            s_splash_bar = nullptr;
            s_splash_label = nullptr;
            s_config_button = nullptr;
            s_on_config = nullptr;

            lvgl_port_unlock();
        }
    } // namespace splash
} // namespace ui
