#include "switch.hpp"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "app/router.hpp"
#include "wifi_manager.h"
#include "rooms.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

namespace ui
{
    namespace controls
    {
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
    } // namespace controls

    namespace toggle
    {
        static const char *TAG_UI_TOGGLE = "UI_TOGGLE";
        static TaskHandle_t s_ha_req_task = nullptr;
        static int64_t s_last_toggle_us = 0;
        static lv_obj_t *s_spinner = nullptr;
        static std::string s_pending_toggle_entity;

        static void ui_show_spinner(void)
        {
            lvgl_port_lock(-1);
            if (s_spinner)
            {
                lv_obj_del(s_spinner);
                s_spinner = nullptr;
            }
            lv_obj_t *scr = lv_screen_active();
            s_spinner = lv_spinner_create(scr);
            lv_obj_set_size(s_spinner, 24, 24);
            lv_obj_align(s_spinner, LV_ALIGN_BOTTOM_MID, 0, -10);
            lvgl_port_unlock();
        }

        static void ui_hide_spinner(void)
        {
            lvgl_port_lock(-1);
            if (s_spinner)
            {
                lv_obj_del(s_spinner);
                s_spinner = nullptr;
            }
            lvgl_port_unlock();
        }

        static void ui_set_switches_enabled(bool enabled)
        {
            lvgl_port_lock(-1);
            for (auto &page : ui::rooms::s_room_pages)
            {
                for (auto &w : page.devices)
                {
                    if (!w.control)
                        continue;
                    ui::controls::set_switch_enabled(w.control, enabled);
                }
            }
            lvgl_port_unlock();
        }

        static void ha_toggle_task(void *arg)
        {
            char *entity = static_cast<char *>(arg);

            s_last_toggle_us = esp_timer_get_time();
            vTaskDelay(pdMS_TO_TICKS(200));

            if (!wifi_manager_is_connected())
            {
                ESP_LOGW(TAG_UI_TOGGLE, "No WiFi, cannot toggle entity");
                if (entity)
                    std::free(entity);
                s_pending_toggle_entity.clear();
                ui_set_switches_enabled(true);
                ui_hide_spinner();
                s_ha_req_task = nullptr;
                vTaskDelete(nullptr);
                return;
            }

            esp_err_t err = router::toggle(entity);

            if (entity)
                std::free(entity);

            if (err != ESP_OK)
            {
                ESP_LOGW(TAG_UI_TOGGLE, "MQTT toggle error: %d", (int)err);
            }

            s_ha_req_task = nullptr;
            s_pending_toggle_entity.clear();
            ui_set_switches_enabled(true);
            ui_hide_spinner();
            vTaskDelete(nullptr);
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
            if (!router::is_connected())
            {
                ESP_LOGW(TAG_UI_TOGGLE, "No MQTT connection for toggle");
                return;
            }
            if (s_ha_req_task != nullptr)
            {
                ESP_LOGW(TAG_UI_TOGGLE, "Toggle already in progress");
                return;
            }

            s_pending_toggle_entity = entity_id;
            ui_set_switches_enabled(false);
            ui_show_spinner();

            if (s_ha_req_task == nullptr)
            {
                char *arg = static_cast<char *>(std::malloc(entity_id.size() + 1));
                if (!arg)
                {
                    ESP_LOGE(TAG_UI_TOGGLE, "No memory for toggle arg");
                    s_pending_toggle_entity.clear();
                    ui_set_switches_enabled(true);
                    ui_hide_spinner();
                    return;
                }
                std::memcpy(arg, entity_id.c_str(), entity_id.size() + 1);

                BaseType_t ok = xTaskCreate(ha_toggle_task, "ha_toggle", 4096, arg, 4, &s_ha_req_task);
                if (ok != pdPASS)
                {
                    std::free(arg);
                    s_ha_req_task = nullptr;
                    ESP_LOGW(TAG_UI_TOGGLE, "Failed to create ha_toggle task");
                    ui_set_switches_enabled(true);
                    s_pending_toggle_entity.clear();
                    ui_hide_spinner();
                }
            }
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
            if (s_ha_req_task != nullptr)
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

    } // namespace toggle
} // namespace ui
