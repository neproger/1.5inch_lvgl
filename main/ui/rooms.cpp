#include "rooms.hpp"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "fonts.h"
#include "icons.h"
#include "state_manager.hpp"

#include <cstdio>

namespace ui
{
    namespace rooms
    {
        static const char *TAG_UI_ROOMS = "UI_ROOMS";

        std::vector<RoomPage> s_room_pages;
        int s_current_room_index = 0;
        int s_current_device_index = 0;

        static void ui_add_switch_widget(RoomPage &page, const state::Entity &ent)
        {
            DeviceWidget w;
            w.entity_id = ent.id;
            w.name = ent.name;
            w.container = lv_obj_create(page.list_container);
            lv_obj_set_style_bg_opa(w.container, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(w.container, 0, 0);
            lv_obj_set_width(w.container, LV_PCT(100));
            lv_obj_set_height(w.container, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_ver(w.container, 8, 0);
            lv_obj_set_style_pad_hor(w.container, 8, 0);
            lv_obj_set_style_pad_row(w.container, 10, 0);
            lv_obj_remove_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(
                w.container,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER,
                LV_FLEX_ALIGN_CENTER);

            w.label = lv_label_create(w.container);
            lv_label_set_text(w.label, ent.name.c_str());
            lv_obj_set_style_text_color(w.label, lv_color_hex(0xE6E6E6), 0);
            lv_obj_set_style_text_font(w.label, &Montserrat_30, 0);

            w.control = lv_switch_create(w.container);
            lv_obj_set_style_width(w.control, 160, LV_PART_MAIN);
            lv_obj_set_style_height(w.control, 70, LV_PART_MAIN);

            bool is_on = (ent.state == "on" || ent.state == "ON" ||
                          ent.state == "true" || ent.state == "TRUE" ||
                          ent.state == "1");
            if (is_on)
            {
                lv_obj_add_state(w.control, LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_clear_state(w.control, LV_STATE_CHECKED);
            }

            page.devices.push_back(std::move(w));
        }

        void ui_build_room_pages()
        {
            s_room_pages.clear();

            const auto &areas = state::areas();
            const auto &entities = state::entities();

            if (areas.empty())
            {
                ESP_LOGW(TAG_UI_ROOMS, "ui_build_room_pages: no areas defined");
                return;
            }

            s_room_pages.reserve(areas.size());

            for (const auto &area : areas)
            {
                RoomPage page;
                page.area_id = area.id;
                page.area_name = area.name;

                page.root = lv_obj_create(NULL);
                lv_obj_set_size(page.root, LV_HOR_RES, LV_VER_RES);
                lv_obj_set_style_bg_color(page.root, lv_color_hex(0x000000), 0);
                lv_obj_set_style_border_width(page.root, 0, 0);
                lv_obj_remove_flag(page.root, LV_OBJ_FLAG_SCROLLABLE);

                page.title_label = lv_label_create(page.root);
                lv_label_set_text(page.title_label, page.area_name.c_str());
                lv_obj_set_style_text_color(page.title_label, lv_color_hex(0xE6E6E6), 0);
                lv_obj_set_style_text_font(page.title_label, &Montserrat_50, 0);
                lv_obj_align(page.title_label, LV_ALIGN_TOP_MID, 0, 30);

                page.list_container = lv_obj_create(page.root);
                lv_obj_set_style_bg_opa(page.list_container, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(page.list_container, 0, 0);
                lv_obj_set_style_pad_row(page.list_container, 20, 0);

                int top = 80;
                lv_obj_set_size(page.list_container, LV_PCT(90), LV_VER_RES - top);
                lv_obj_align(page.list_container, LV_ALIGN_TOP_MID, 0, top);

                lv_obj_set_scroll_dir(page.list_container, LV_DIR_VER);
                lv_obj_set_scrollbar_mode(page.list_container, LV_SCROLLBAR_MODE_AUTO);
                lv_obj_set_flex_flow(page.list_container, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_flex_align(page.list_container,
                                      LV_FLEX_ALIGN_START,
                                      LV_FLEX_ALIGN_START,
                                      LV_FLEX_ALIGN_START);

                for (size_t i = 0; i < entities.size(); ++i)
                {
                    const auto &ent = entities[i];
                    if (ent.area_id != area.id)
                        continue;

                    ui_add_switch_widget(page, ent);
                }

                s_room_pages.push_back(std::move(page));
            }
        }

    } // namespace rooms
} // namespace ui
