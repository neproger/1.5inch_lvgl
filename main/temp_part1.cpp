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
#include "http_manager.hpp"
#include "icons.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdio>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG_UI = "UI";
static TaskHandle_t s_ha_req_task = NULL;
static int64_t s_last_toggle_us = 0; /* simple cooldown to avoid hammering */

static std::vector<int> s_state_subscriptions;
static int64_t s_last_input_us = 0;
static lv_obj_t *s_spinner = NULL;
static std::string s_pending_toggle_entity;

static lv_obj_t *s_splash_root = NULL;
static lv_obj_t *s_splash_bar = NULL;

enum class UiMode
{
    Rooms,
    Screensaver
};

static UiMode s_ui_mode = UiMode::Rooms;
static lv_obj_t *s_screensaver_root = NULL;
static lv_obj_t *s_weather_temp_label = NULL;
static lv_obj_t *s_weather_cond_label = NULL;
static lv_obj_t *s_weather_icon = NULL;
static lv_timer_t *s_idle_timer = NULL;
static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_date_label = NULL;
static lv_obj_t *s_week_day_label = NULL;

static lv_timer_t *s_clock_timer = NULL;
static const uint32_t kScreensaverTimeoutMs = 10000;

namespace ui
{
    namespace rooms
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
            lv_obj_t *weather_label = nullptr;
            std::vector<DeviceWidget> devices;
        };

        static std::vector<RoomPage> s_room_pages;
        static int s_current_room_index = 0;
        static int s_current_device_index = 0;

        static void ui_build_room_pages();
        static void ui_update_weather_label();
    } // namespace rooms
} // namespace ui

using ui::rooms::DeviceWidget;
using ui::rooms::RoomPage;
using ui::rooms::s_current_device_index;
using ui::rooms::s_current_room_index;
using ui::rooms::s_room_pages;
using ui::rooms::ui_build_room_pages;
using ui::rooms::ui_update_weather_label;

static void handle_single_click(void);
static void ha_toggle_task(void *arg);
static void trigger_toggle_for_entity(const std::string &entity_id);
static void switch_event_cb(lv_event_t *e);
static void on_state_entity_changed(const state::Entity &e);
static void ui_show_spinner(void);
static void ui_hide_spinner(void);
static void ui_set_switches_enabled(bool enabled);
static void ui_show_splash_screen(void);
static void ui_update_splash_progress(int percent);
static void ui_destroy_splash_screen(void);
static void on_weather_updated_from_http(void);
static void ui_build_screensaver(void);
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

extern "C" void ui_show_boot_splash(void)
{
    ui_show_splash_screen();
}

extern "C" void ui_update_boot_splash(int percent)
{
    ui_update_splash_progress(percent);
}

extern "C" void ui_hide_boot_splash(void)
{
    ui_destroy_splash_screen();
}

extern "C" void ui_init_screensaver_support(void)
{
    ui_build_screensaver();
    ui_update_weather_label();

    if (s_idle_timer == NULL)
    {
        s_idle_timer = lv_timer_create(idle_timer_cb, 500, NULL);
    }
    if (s_clock_timer == NULL)
    {
        s_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    }
}

extern "C" void ui_start_weather_polling(void)
{
    http_manager::start_weather_polling(&on_weather_updated_from_http);
}

static void on_weather_updated_from_http(void)
{
    lvgl_port_lock(-1);
    ui_update_weather_label();
    lvgl_port_unlock();
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
