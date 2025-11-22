#include "rooms.hpp"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "fonts.h"
#include "icons.h"
#include "state_manager.hpp"
#include "switch.hpp"
#include "screensaver.hpp"
#include "app/app_events.hpp"

#include <cstdio>

namespace ui
{
    namespace rooms
    {
        static const char *TAG_UI_ROOMS = "UI_ROOMS";
        static bool s_nav_handler_registered = false;

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

            if (!s_nav_handler_registered)
            {
                esp_event_handler_instance_t inst = nullptr;
                (void)esp_event_handler_instance_register(
                    APP_EVENTS,
                    app_events::NAVIGATE_ROOM,
                    [](void * /*arg*/, esp_event_base_t base, int32_t id, void *event_data)
                    {
                        if (base != APP_EVENTS || id != app_events::NAVIGATE_ROOM)
                        {
                            return;
                        }

                        const auto *payload = static_cast<const app_events::NavigateRoomPayload *>(event_data);
                        if (!payload)
                        {
                            return;
                        }
                        int delta = payload->delta;

                        lvgl_port_lock(-1);

                        if (delta > 0)
                        {
                            show_room_relative(+1, LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
                        }
                        else if (delta < 0)
                        {
                            show_room_relative(-1, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
                        }

                        if (!s_room_pages.empty())
                        {
                            const RoomPage &page = s_room_pages[s_current_room_index];
                            const char *room_name = page.area_name.c_str();
                            ESP_LOGI(TAG_UI_ROOMS, "NAVIGATE_ROOM delta=%d | room=%d/%d %s",
                                     delta,
                                     s_current_room_index,
                                     static_cast<int>(s_room_pages.size()),
                                     room_name);
                        }

                        lvgl_port_unlock();
                    },
                    nullptr,
                    &inst);
                s_nav_handler_registered = true;
            }
        }

        void show_initial_room()
        {
            if (s_room_pages.empty())
            {
                return;
            }

            s_current_room_index = 0;
            s_current_device_index = 0;
            lv_disp_load_scr(s_room_pages[0].root);
        }

        void show_room_relative(int delta, lv_screen_load_anim_t anim_type)
        {
            if (s_room_pages.empty())
            {
                return;
            }

            int rooms = static_cast<int>(s_room_pages.size());
            if (rooms <= 0)
            {
                return;
            }

            int new_index = s_current_room_index + delta;
            int idx = new_index % rooms;
            if (idx < 0)
            {
                idx += rooms;
            }

            s_current_room_index = idx;
            s_current_device_index = 0;

            lv_obj_t *scr = s_room_pages[s_current_room_index].root;
            if (!scr)
            {
                return;
            }

            lv_scr_load_anim(scr, anim_type, 300, 0, false);
        }

        bool get_current_entity_id(std::string &out_entity_id)
        {
            out_entity_id.clear();

            if (s_room_pages.empty())
            {
                return false;
            }

            if (s_current_room_index < 0 ||
                s_current_room_index >= static_cast<int>(s_room_pages.size()))
            {
                return false;
            }

            const RoomPage &page = s_room_pages[s_current_room_index];
            if (page.devices.empty())
            {
                return false;
            }

            if (s_current_device_index < 0 ||
                s_current_device_index >= static_cast<int>(page.devices.size()))
            {
                return false;
            }

            out_entity_id = page.devices[static_cast<size_t>(s_current_device_index)].entity_id;
            return !out_entity_id.empty();
        }

        bool find_entity_for_control(lv_obj_t *control, std::string &out_entity_id)
        {
            out_entity_id.clear();

            if (!control)
            {
                return false;
            }

            for (const auto &page : s_room_pages)
            {
                for (const auto &w : page.devices)
                {
                    if (w.control == control)
                    {
                        out_entity_id = w.entity_id;
                        return !out_entity_id.empty();
                    }
                }
            }

            return false;
        }

        void on_entity_state_changed(const state::Entity &e)
        {
            if (s_room_pages.empty())
            {
                return;
            }

            bool updated = false;
            lvgl_port_lock(-1);
            for (auto &page : s_room_pages)
            {
                if (page.area_id != e.area_id)
                    continue;

                for (auto &w : page.devices)
                {
                    if (w.entity_id != e.id)
                        continue;

                    if (w.control)
                    {
                        bool is_on = (e.state == "on" || e.state == "ON" ||
                                      e.state == "true" || e.state == "TRUE" ||
                                      e.state == "1");
                        ui::controls::set_switch_state(w.control, is_on);
                    }
                    updated = true;
                    break;
                }
                if (updated)
                    break;
            }
            lvgl_port_unlock();
        }

    } // namespace rooms
} // namespace ui
