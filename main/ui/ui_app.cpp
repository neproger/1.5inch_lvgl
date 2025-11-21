#include "ui_app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "app/router.hpp"
#include "state_manager.hpp"
#include "wifi_manager.h"
#include "rooms.hpp"
#include "screensaver.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG_UI = "UI";
static TaskHandle_t s_ha_req_task = NULL;
static int64_t s_last_toggle_us = 0; /* simple cooldown to avoid hammering */

static std::vector<int> s_state_subscriptions;
static int64_t s_last_input_us = 0;
static lv_obj_t *s_spinner = NULL;
static std::string s_pending_toggle_entity;

enum class UiMode
{
    Rooms,
    Screensaver
};

static UiMode s_ui_mode = UiMode::Rooms;
static lv_timer_t *s_idle_timer = NULL;
static lv_timer_t *s_clock_timer = NULL;
static const uint32_t kScreensaverTimeoutMs = 10000;

using ui::rooms::DeviceWidget;
using ui::rooms::RoomPage;
using ui::rooms::s_current_device_index;
using ui::rooms::s_current_room_index;
using ui::rooms::s_room_pages;
using ui::rooms::ui_build_room_pages;
using ui::screensaver::s_screensaver_root;

static void handle_single_click(void);
static void ha_toggle_task(void *arg);
static void trigger_toggle_for_entity(const std::string &entity_id);
static void switch_event_cb(lv_event_t *e);
static void on_state_entity_changed(const state::Entity &e);
static void ui_show_spinner(void);
static void ui_hide_spinner(void);
static void ui_set_switches_enabled(bool enabled);
static void idle_timer_cb(lv_timer_t *timer);
static void screensaver_input_cb(lv_event_t *e);
static void root_gesture_cb(lv_event_t *e);
static void ui_load_room_screen(int new_index, lv_screen_load_anim_t anim_type);
static void clock_timer_cb(lv_timer_t *timer);
void ui_app_init(void)
{
    // Track last user input timestamp
    s_last_input_us = esp_timer_get_time();
    // Build UI room pages
    ui_build_room_pages();

    // Attach gesture and switch callbacks
    for (auto &page : s_room_pages)
    {
        if (page.root)
        {
            lv_obj_add_event_cb(page.root, root_gesture_cb, LV_EVENT_GESTURE, nullptr);
        }
        for (auto &w : page.devices)
        {
            if (w.control)
            {
                lv_obj_add_event_cb(w.control, switch_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
            }
        }
    }

    // Subscribe to entity updates and apply initial state
    {
        const auto &ents = state::entities();
        for (const auto &e : ents)
        {
            int id = state::subscribe_entity(e.id, &on_state_entity_changed);
            if (id > 0)
                s_state_subscriptions.push_back(id);
        }
        // Apply current state to widgets (bootstrap after MQTT)
        for (const auto &e : ents)
        {
            on_state_entity_changed(e);
        }
    }

    if (!s_room_pages.empty())
    {
        s_current_room_index = 0;
        s_current_device_index = 0;
        lv_disp_load_scr(s_room_pages[0].root);
    }
}

void ui_init_screensaver_support(void)
{
    ui::screensaver::ui_build_screensaver();
    ui::screensaver::ui_update_weather_and_clock();

    if (s_idle_timer == NULL)
    {
        s_idle_timer = lv_timer_create(idle_timer_cb, 500, NULL);
    }
    if (s_clock_timer == NULL)
    {
        s_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    }

    if (s_screensaver_root)
    {
        lv_obj_add_event_cb(s_screensaver_root, screensaver_input_cb, LV_EVENT_PRESSED, nullptr);
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

    lv_display_trigger_activity(NULL);

    if (s_ui_mode == UiMode::Screensaver)
    {
        s_ui_mode = UiMode::Rooms;
        if (!s_room_pages.empty())
        {
            int rooms = static_cast<int>(s_room_pages.size());
            if (s_current_room_index < 0 || s_current_room_index >= rooms)
            {
                s_current_room_index = 0;
            }
            lv_disp_load_scr(s_room_pages[s_current_room_index].root);
        }
        return;
    }

    switch (ev)
    {
    case 0: // next room
        ui_load_room_screen(s_current_room_index + 1, LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
        break;
    case 1: // previous room
        ui_load_room_screen(s_current_room_index - 1, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
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

    lv_display_trigger_activity(NULL);

    if (s_ui_mode == UiMode::Screensaver)
    {
        s_ui_mode = UiMode::Rooms;
        if (!s_room_pages.empty())
        {
            int rooms = static_cast<int>(s_room_pages.size());
            if (s_current_room_index < 0 || s_current_room_index >= rooms)
            {
                s_current_room_index = 0;
            }
            lv_disp_load_scr(s_room_pages[s_current_room_index].root);
        }
        return;
    }

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
    if (s_ha_req_task != NULL)
    {
        ESP_LOGW(TAG_UI, "Toggle request already in progress");
        return;
    }
    if (!router::is_connected())
    {
        ESP_LOGW(TAG_UI, "No MQTT connection for toggle");
        return;
    }

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

    trigger_toggle_for_entity(entity_id);
}

static void trigger_toggle_for_entity(const std::string &entity_id)
{
    if (entity_id.empty())
    {
        ESP_LOGW(TAG_UI, "trigger_toggle_for_entity: empty entity_id");
        return;
    }

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
    if (s_ha_req_task != NULL)
    {
        ESP_LOGW(TAG_UI, "Toggle already in progress");
        return;
    }

    s_pending_toggle_entity = entity_id;
    ui_set_switches_enabled(false);
    ui_show_spinner();

    if (s_ha_req_task == NULL)
    {
        char *arg = static_cast<char *>(std::malloc(entity_id.size() + 1));
        if (!arg)
        {
            ESP_LOGE(TAG_UI, "No memory for toggle arg");
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
            s_ha_req_task = NULL;
            ESP_LOGW(TAG_UI, "Failed to create ha_toggle task");
            ui_set_switches_enabled(true);
            s_pending_toggle_entity.clear();
            ui_hide_spinner();
        }
    }
}

static void ha_toggle_task(void *arg)
{
    char *entity = static_cast<char *>(arg);

    s_last_toggle_us = esp_timer_get_time();
    vTaskDelay(pdMS_TO_TICKS(200));

    if (!wifi_manager_is_connected())
    {
        ESP_LOGW(TAG_UI, "No WiFi, cannot toggle entity");
        if (entity)
            std::free(entity);
        s_pending_toggle_entity.clear();
        ui_set_switches_enabled(true);
        ui_hide_spinner();
        s_ha_req_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    esp_err_t err = router::toggle(entity);

    if (entity)
        std::free(entity);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG_UI, "MQTT toggle error: %d", (int)err);
    }

    s_ha_req_task = NULL;
    s_pending_toggle_entity.clear();
    ui_set_switches_enabled(true);
    ui_hide_spinner();
    vTaskDelete(NULL);
}

static void switch_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_VALUE_CHANGED)
    {
        return;
    }

    lv_obj_t *sw = static_cast<lv_obj_t *>(lv_event_get_target(e));
    if (lv_obj_has_state(sw, LV_STATE_DISABLED))
    {
        ESP_LOGI(TAG_UI, "Toggle ignored, control disabled");
        return;
    }
    if (s_ha_req_task != NULL)
    {
        ESP_LOGI(TAG_UI, "Toggle in progress, ignoring switch");
        return;
    }
    std::string entity_id;

    for (const auto &page : s_room_pages)
    {
        for (const auto &w : page.devices)
        {
            if (w.control == sw)
            {
                entity_id = w.entity_id;
                break;
            }
        }
        if (!entity_id.empty())
            break;
    }

    if (entity_id.empty())
    {
        ESP_LOGW(TAG_UI, "switch_event_cb: control without entity");
        return;
    }

    trigger_toggle_for_entity(entity_id);
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

static void ui_set_switches_enabled(bool enabled)
{
    lvgl_port_lock(-1);
    for (auto &page : s_room_pages)
    {
        for (auto &w : page.devices)
        {
            if (!w.control)
                continue;
            if (enabled)
            {
                lv_obj_clear_state(w.control, LV_STATE_DISABLED);
            }
            else
            {
                lv_obj_add_state(w.control, LV_STATE_DISABLED);
            }
        }
    }
    lvgl_port_unlock();
}

static void on_state_entity_changed(const state::Entity &e)
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
                if (is_on)
                {
                    lv_obj_add_state(w.control, LV_STATE_CHECKED);
                }
                else
                {
                    lv_obj_clear_state(w.control, LV_STATE_CHECKED);
                }
            }
            updated = true;
            break;
        }
        if (updated)
            break;
    }
    lvgl_port_unlock();
}

static void idle_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    lv_display_t *disp = lv_display_get_default();
    if (!disp)
    {
        return;
    }

    uint32_t inactive_ms = lv_display_get_inactive_time(disp);

    if (s_ui_mode == UiMode::Rooms)
    {
        if (inactive_ms >= kScreensaverTimeoutMs)
        {
            ui::screensaver::show();
            s_ui_mode = UiMode::Screensaver;
        }
    }
}

static void screensaver_input_cb(lv_event_t *e)
{
    if (s_ui_mode != UiMode::Screensaver)
    {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_PRESSED)
    {
        return;
    }

    s_ui_mode = UiMode::Rooms;
    if (!s_room_pages.empty())
    {
        int rooms = static_cast<int>(s_room_pages.size());
        if (s_current_room_index < 0 || s_current_room_index >= rooms)
        {
            s_current_room_index = 0;
        }
        ui::screensaver::hide_to_room(s_room_pages[s_current_room_index].root);
    }

    lv_indev_reset(NULL, nullptr);
}

static void root_gesture_cb(lv_event_t *e)
{
    if (s_ui_mode != UiMode::Rooms)
    {
        return;
    }

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev)
    {
        return;
    }

    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT)
    {
        ui_load_room_screen(s_current_room_index + 1, LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
    }
    else if (dir == LV_DIR_RIGHT)
    {
        ui_load_room_screen(s_current_room_index - 1, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
    }
}

static void clock_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    lvgl_port_lock(-1);
    ui::screensaver::ui_update_weather_and_clock();
    lvgl_port_unlock();
}

static void ui_load_room_screen(int new_index, lv_screen_load_anim_t anim_type)
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

