#include "switch.hpp"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "app/app_events.hpp"
#include "rooms.hpp"
#include "state_manager.hpp"
#include "fonts.h"

#include <vector>
#include <utility>

namespace ui
{
    namespace controls
    {
        static std::vector<std::pair<lv_obj_t *, lv_obj_t *>> s_switch_rings;
        static lv_obj_t *find_ring_for_control(lv_obj_t *control)
        {
            if (!control)
            {
                return nullptr;
            }

            for (auto &entry : s_switch_rings)
            {
                if (entry.first == control)
                {
                    return entry.second;
                }
            }

            return nullptr;
        }

        void set_switch_state(lv_obj_t *control, bool is_on)
        {
            if (!control)
            {
                return;
            }

            if (is_on)
            {
                lv_obj_add_state(control, LV_STATE_CHECKED);
            }
            else
            {
                lv_obj_clear_state(control, LV_STATE_CHECKED);
            }

            lv_obj_t *ring = find_ring_for_control(control);
            if (ring)
            {
                lv_color_t color = is_on ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000);
                lv_obj_set_style_arc_color(ring, color, LV_PART_INDICATOR);
            }
        }

        void set_switch_enabled(lv_obj_t *control, bool enabled)
        {
            if (!control)
            {
                return;
            }

            if (enabled)
            {
                lv_obj_clear_state(control, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(control, LV_STATE_DISABLED);
            }
        }

        void ui_add_switch_widget(
            lv_obj_t *parent,
            const state::Entity &ent,
            lv_obj_t *&out_label,
            lv_obj_t *&out_control,
            lv_obj_t *&out_ring)
        {
            out_label = nullptr;
            out_control = nullptr;
            out_ring = nullptr;

            if (!parent)
            {
                return;
            }

            bool is_on = (ent.state == "on" || ent.state == "ON" ||
                          ent.state == "true" || ent.state == "TRUE" ||
                          ent.state == "1");

            // Create ring inside tile (same parent as label/switch)
            lv_obj_t *ring = lv_arc_create(parent);
            lv_obj_remove_style_all(ring);
            lv_obj_set_size(ring, LV_HOR_RES, LV_VER_RES);
            lv_obj_center(ring);
            lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_flag(ring, LV_OBJ_FLAG_GESTURE_BUBBLE);

            lv_arc_set_bg_angles(ring, 0, 360);
            lv_arc_set_angles(ring, 0, 360);

            lv_obj_set_style_arc_width(ring, 4, LV_PART_MAIN);
            lv_obj_set_style_arc_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);

            lv_obj_set_style_arc_width(ring, 4, LV_PART_INDICATOR);
            lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR);
            lv_obj_center(ring);

            // Create label inside tile container so flex layout works
            lv_obj_t *label = lv_label_create(ring);
            lv_label_set_text(label, ent.name.c_str());
            lv_obj_set_style_text_color(label, lv_color_hex(0xE6E6E6), 0);
            lv_obj_set_style_text_font(label, &Montserrat_40, 0);
            lv_obj_align_to(label, ring, LV_ALIGN_CENTER, 0, -20);

            // Create switch inside tile container under label
            lv_obj_t *control = lv_switch_create(ring);
            lv_obj_set_style_width(control, 160, LV_PART_MAIN);
            lv_obj_set_style_height(control, 70, LV_PART_MAIN);
            lv_obj_align_to(control, label, LV_ALIGN_OUT_BOTTOM_MID, 0, 30);


            s_switch_rings.emplace_back(control, ring);

            set_switch_state(control, is_on);

            out_label = label;
            out_control = control;
            out_ring = ring;
        }
    } // namespace controls

    namespace toggle
    {
        static const char *TAG_UI_TOGGLE = "UI_TOGGLE";
        static int64_t s_last_toggle_us = 0;
        static lv_obj_t *s_spinner = nullptr;
        static std::string s_pending_toggle_entity;

        static void ui_show_spinner(void)
        {
            if (s_spinner)
            {
                lv_obj_del(s_spinner);
                s_spinner = nullptr;
            }
            lv_obj_t *scr = lv_screen_active();
            s_spinner = lv_spinner_create(scr);
            lv_obj_set_size(s_spinner, 50, 50);
            lv_obj_align(s_spinner, LV_ALIGN_BOTTOM_MID, 0, -10);
        }

        static void ui_hide_spinner(void)
        {
            if (s_spinner)
            {
                lv_obj_del(s_spinner);
                s_spinner = nullptr;
            }
        }

        static void ui_set_switches_enabled(bool enabled)
        {
            for (auto &page : ui::rooms::s_room_pages)
            {
                for (auto &w : page.devices)
                {
                    if (!w.control)
                        continue;
                    ui::controls::set_switch_enabled(w.control, enabled);
                }
            }
        }

        static void on_toggle_result(void * /*arg*/, esp_event_base_t base, int32_t id, void *event_data)
        {
            if (base != APP_EVENTS || id != app_events::TOGGLE_RESULT)
            {
                return;
            }

            const auto *payload = static_cast<const app_events::ToggleResultPayload *>(event_data);
            if (!payload)
            {
                return;
            }

            // Only react for current pending entity (if any)
            if (!s_pending_toggle_entity.empty() && s_pending_toggle_entity != payload->entity_id)
            {
                return;
            }

            lvgl_port_lock(-1);
            ui_set_switches_enabled(true);
            ui_hide_spinner();
            lvgl_port_unlock();

            s_pending_toggle_entity.clear();
        }

        void trigger_toggle_for_entity(const std::string &entity_id)
        {
            if (entity_id.empty())
            {
                ESP_LOGW(TAG_UI_TOGGLE, "trigger_toggle_for_entity: empty entity_id");
                return;
            }

            int64_t now = esp_timer_get_time();
            if (now - s_last_toggle_us < 700 * 1000)
            {
                return;
            }
            if (!s_pending_toggle_entity.empty())
            {
                ESP_LOGW(TAG_UI_TOGGLE, "Toggle already in progress");
                return;
            }

            s_pending_toggle_entity = entity_id;
            ui_set_switches_enabled(false);
            ui_show_spinner();

            s_last_toggle_us = now;
            (void)app_events::post_toggle_request(entity_id.c_str(), now, false);
        }

        void switch_event_cb(lv_event_t *e)
        {
            lv_event_code_t code = lv_event_get_code(e);
            if (code != LV_EVENT_VALUE_CHANGED)
            {
                return;
            }

            lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
            if (lv_obj_has_state(sw, LV_STATE_DISABLED))
            {
                ESP_LOGI(TAG_UI_TOGGLE, "Toggle ignored, control disabled");
                return;
            }
            if (!s_pending_toggle_entity.empty())
            {
                ESP_LOGI(TAG_UI_TOGGLE, "Toggle in progress, ignoring switch");
                return;
            }
            std::string entity_id;
            if (!ui::rooms::find_entity_for_control(sw, entity_id))
            {
                ESP_LOGW(TAG_UI_TOGGLE, "switch_event_cb: control without entity");
                return;
            }

            trigger_toggle_for_entity(entity_id);
        }

        esp_err_t init()
        {
            static bool s_registered = false;
            if (s_registered)
            {
                return ESP_OK;
            }

            esp_event_handler_instance_t inst = nullptr;
            esp_err_t err = esp_event_handler_instance_register(
                APP_EVENTS,
                app_events::TOGGLE_RESULT,
                &on_toggle_result,
                nullptr,
                &inst);

            if (err != ESP_OK)
            {
                ESP_LOGW(TAG_UI_TOGGLE, "failed to register TOGGLE_RESULT handler: %s", esp_err_to_name(err));
            }
            else
            {
                s_registered = true;
            }

            return err;
        }

    } // namespace toggle
} // namespace ui
