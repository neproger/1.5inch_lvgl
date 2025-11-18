#include "ui_app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "fonts.h"
#include "app/router.hpp"
#include "app/entities.hpp"
#include "state_manager.hpp"
#include "devices_init.h"
#include "wifi_manager.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <cstdlib>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG_UI = "UI";
static TaskHandle_t s_ha_req_task = NULL;
static int64_t s_last_toggle_us = 0; /* simple cooldown to avoid hammering */

static volatile bool s_state_dirty = true;
static std::vector<int> s_state_subscriptions;

static int64_t s_last_input_us = 0;
static lv_obj_t *s_spinner = NULL;
static std::string s_pending_toggle_entity;

namespace // room pages
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

    static std::vector<RoomPage> s_room_pages;
    static int s_current_room_index = 0;
    static int s_current_device_index = 0;

    static void ui_build_room_pages();
}

static void handle_single_click(void);
static void ha_toggle_task(void *arg);
static void ui_show_current_item(void);
static void ui_refresh_current_state(void);
static void on_state_entity_changed(const state::Entity &e);
static void ui_show_spinner(void);
static void ui_hide_spinner(void);
static lv_obj_t *background = nullptr;
void ui_app_init(void)
{
    // Время последнего ввода (как у тебя было)
    s_last_input_us = esp_timer_get_time();
    background = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(background, lv_color_hex(0x000000), 0);
    ui_build_room_pages();

    // 3. Таймер для UI — оставляем твой код
    {
        const auto &ents = state::entities();
        for (const auto &e : ents)
        {
            int id = state::subscribe_entity(e.id, &on_state_entity_changed);
            if (id > 0)
                s_state_subscriptions.push_back(id);
        }
    }
}

static const char *knob_event_table[] = {
    "KNOB_RIGHT",
    "KNOB_LEFT",
    "KNOB_H_LIM",
    "KNOB_L_LIM",
    "KNOB_ZERO",
};

extern "C" void LVGL_knob_event(void *event)
{
    int ev = (int)(intptr_t)event;
    s_last_input_us = esp_timer_get_time();

    switch (ev)
    {
    case 0:
        if (!s_room_pages.empty())
        {
            int rooms = static_cast<int>(s_room_pages.size());
            s_current_room_index = (s_current_room_index + 1) % rooms;
            s_current_device_index = 0;
            lv_disp_load_scr(s_room_pages[s_current_room_index].root);
            ui_show_current_item();
        }
        break;
    case 1:
        if (!s_room_pages.empty())
        {
            int rooms = static_cast<int>(s_room_pages.size());
            s_current_room_index = (s_current_room_index - 1 + rooms) % rooms;
            s_current_device_index = 0;
            lv_disp_load_scr(s_room_pages[s_current_room_index].root);
            ui_show_current_item();
        }
        break;
    default:
        break;
    }

    if (!s_room_pages.empty())
    {
        const RoomPage &page = s_room_pages[s_current_room_index];
        const char *room_name = page.area_name.c_str();
        ESP_LOGI(TAG_UI, "%s | room=%d/%d %s",
                 knob_event_table[ev],
                 s_current_room_index,
                 static_cast<int>(s_room_pages.size()),
                 room_name);
    }
}

static const char *button_event_table[] = {
    "PRESS_DOWN",
    "PRESS_UP",
    "PRESS_REPEAT",
    "PRESS_REPEAT_DONE",
    "SINGLE_CLICK",
    "DOUBLE_CLICK",
    "MULTIPLE_CLICK",
    "LONG_PRESS_START",
    "LONG_PRESS_HOLD",
    "LONG_PRESS_UP",
    "PRESS_END",
};

extern "C" void LVGL_button_event(void *event)
{
    int ev = (int)(intptr_t)event;
    s_last_input_us = esp_timer_get_time();

    const int table_sz = sizeof(button_event_table) / sizeof(button_event_table[0]);
    const char *label = (ev >= 0 && ev < table_sz) ? button_event_table[ev] : "BUTTON_UNKNOWN";
    switch (ev)
    {
    case 4:
        ESP_LOGI(TAG_UI, "%s", label);
        handle_single_click();
        break;
    default:
        break;
    }
}

static void handle_single_click(void)
{
    int64_t now = esp_timer_get_time();
    if (now - s_last_toggle_us < 700 * 1000)
    {
        return;
    }
    if (!router::is_connected())
    {
        ESP_LOGW(TAG_UI, "No MQTT connection for toggle");
        return;
    }

    // Determine current entity ID and show spinner
    std::string entity_id;

    if (!s_room_pages.empty() &&
        s_current_room_index >= 0 &&
        s_current_room_index < static_cast<int>(s_room_pages.size()))
    {
        const RoomPage &page = s_room_pages[s_current_room_index];
        if (!page.devices.empty() &&
            s_current_device_index >= 0 &&
            s_current_device_index < static_cast<int>(page.devices.size()))
        {
            entity_id = page.devices[static_cast<size_t>(s_current_device_index)].entity_id;
        }
    }

    if (entity_id.empty())
    {
        ESP_LOGW(TAG_UI, "No entity selected for toggle");
        return;
    }

    s_pending_toggle_entity = entity_id;
    ui_show_spinner();

    if (s_ha_req_task == NULL)
    {
        char *arg = static_cast<char *>(std::malloc(entity_id.size() + 1));
        if (!arg)
        {
            ESP_LOGE(TAG_UI, "No memory for toggle arg");
            s_pending_toggle_entity.clear();
            ui_hide_spinner();
            return;
        }
        std::memcpy(arg, entity_id.c_str(), entity_id.size() + 1);

        BaseType_t ok = xTaskCreate(ha_toggle_task, "ha_toggle", 4096, arg, 4, &s_ha_req_task);
        if (ok != pdPASS)
        {
            std::free(arg);
            s_ha_req_task = NULL;
            ESP_LOGW(TAG_UI, "Failed to create ha_toggle task");
            s_pending_toggle_entity.clear();
            ui_hide_spinner();
        }
    }
}

static void ha_toggle_task(void *arg)
{
    char *entity = static_cast<char *>(arg);

    if (!wifi_manager_is_connected())
    {
        ESP_LOGW(TAG_UI, "No WiFi, cannot toggle entity");
        if (entity)
            std::free(entity);
        s_pending_toggle_entity.clear();
        ui_hide_spinner();
        s_ha_req_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = router::toggle(entity);

    if (entity)
        std::free(entity);

    if (err == ESP_OK)
    {
        s_last_toggle_us = esp_timer_get_time();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    else
    {
        ESP_LOGW(TAG_UI, "MQTT toggle error: %d", (int)err);
        s_pending_toggle_entity.clear();
        ui_hide_spinner();
    }
    s_ha_req_task = NULL;
    vTaskDelete(NULL);
}

static void ui_show_current_item(void)
{
    if (!s_room_pages.empty() &&
        s_current_room_index >= 0 &&
        s_current_room_index < static_cast<int>(s_room_pages.size()))
    {
        const RoomPage &page = s_room_pages[s_current_room_index];
        if (!page.devices.empty() &&
            s_current_device_index >= 0 &&
            s_current_device_index < static_cast<int>(page.devices.size()))
        {
            const DeviceWidget &w = page.devices[static_cast<size_t>(s_current_device_index)];
            (void)w;
            ui_refresh_current_state();
            return;
        }
    }

    ui_refresh_current_state();
}

static void ui_refresh_current_state(void)
{
    const char *val = nullptr;

    if (!s_room_pages.empty() &&
        s_current_room_index >= 0 &&
        s_current_room_index < static_cast<int>(s_room_pages.size()))
    {
        const RoomPage &page = s_room_pages[s_current_room_index];
        if (!page.devices.empty() &&
            s_current_device_index >= 0 &&
            s_current_device_index < static_cast<int>(page.devices.size()))
        {
            const DeviceWidget &w = page.devices[static_cast<size_t>(s_current_device_index)];
            const state::Entity *e = state::find_entity(w.entity_id);
            if (e)
            {
                val = e->state.c_str();
            }
        }
    }

    (void)val; // Currently per-entity widgets are updated via on_state_entity_changed
}

static void ui_show_spinner(void)
{
    lvgl_port_lock(-1);
    if (s_spinner)
    {
        lv_obj_del(s_spinner);
        s_spinner = NULL;
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
        s_spinner = NULL;
    }
    lvgl_port_unlock();
}

static void on_state_entity_changed(const state::Entity &e)
{
    s_state_dirty = true;

    // Update widgets for this entity
    if (!s_room_pages.empty())
    {
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
                    lvgl_port_lock(-1);
                    if (is_on)
                    {
                        lv_obj_add_state(w.control, LV_STATE_CHECKED);
                    }
                    else
                    {
                        lv_obj_clear_state(w.control, LV_STATE_CHECKED);
                    }
                    lvgl_port_unlock();
                }
                break;
            }
        }
    }

    // If this is the entity we just toggled – hide spinner
    if (!s_pending_toggle_entity.empty() && e.id == s_pending_toggle_entity)
    {
        s_pending_toggle_entity.clear();
        ui_hide_spinner();
    }
}

namespace // room pages impl
{
    static void ui_build_room_pages()
    {
        s_room_pages.clear();

        const auto &areas = state::areas();
        const auto &entities = state::entities();

        if (areas.empty())
        {
            ESP_LOGW(TAG_UI, "ui_build_room_pages: no areas defined");
            return;
        }

        s_room_pages.reserve(areas.size());

        for (const auto &area : areas)
        {
            RoomPage page;
            page.area_id = area.id;
            page.area_name = area.name;

            page.root = lv_obj_create(NULL);
            lv_obj_set_style_bg_color(page.root, lv_color_hex(0x000000), 0);

            page.title_label = lv_label_create(page.root);
            lv_label_set_text(page.title_label, page.area_name.c_str());
            lv_obj_set_style_text_color(page.title_label, lv_color_hex(0xE6E6E6), 0);
            lv_obj_set_style_text_font(page.title_label, &Montserrat_20, 0);
            lv_obj_align(page.title_label, LV_ALIGN_TOP_MID, 0, 40);

            page.list_container = lv_obj_create(page.root);
            lv_obj_set_style_bg_opa(page.list_container, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(page.list_container, 0, 0);
            lv_obj_set_size(page.list_container, LV_PCT(100), LV_PCT(100));
            lv_obj_align(page.list_container, LV_ALIGN_BOTTOM_MID, 0, -10);

            // Добавляем виджеты устройств для этой комнаты
            for (size_t i = 0; i < entities.size(); ++i)
            {
                const auto &ent = entities[i];
                if (ent.area_id != area.id)
                    continue;

                DeviceWidget w;
                w.entity_id = ent.id;
                w.name = ent.name;
                w.container = lv_obj_create(page.list_container);
                lv_obj_set_style_bg_opa(w.container, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(w.container, 0, 0);
                lv_obj_set_width(w.container, LV_PCT(100));
                lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_ROW);

                w.label = lv_label_create(w.container);
                lv_label_set_text(w.label, ent.name.c_str());
                lv_obj_set_style_text_color(w.label, lv_color_hex(0xE6E6E6), 0);
                lv_obj_set_style_text_font(w.label, &Montserrat_20, 0);
                lv_obj_align(w.label, LV_ALIGN_LEFT_MID, 8, 0);

                // Switch control for binary entities (initial state from current value)
                w.control = lv_switch_create(w.container);
                lv_obj_align(w.control, LV_ALIGN_RIGHT_MID, -8, 0);

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

            s_room_pages.push_back(std::move(page));
        }
    }

} // namespace

// Legacy overlay labels (status/info) removed in new UI:
// each entity is represented by its own widget on the room page.
