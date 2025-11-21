#include "ui_app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "state_manager.hpp"
#include "rooms.hpp"
#include "screensaver.hpp"
#include "switch.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

static const char *TAG_UI = "UI";

static std::vector<int> s_state_subscriptions;
static int64_t s_last_input_us = 0;

using ui::rooms::DeviceWidget;
using ui::rooms::RoomPage;
using ui::rooms::s_current_device_index;
using ui::rooms::s_current_room_index;
using ui::rooms::s_room_pages;
using ui::rooms::ui_build_room_pages;
using ui::screensaver::s_screensaver_root;

static void handle_single_click(void);
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
            lv_obj_add_event_cb(page.root, ui::rooms::root_gesture_cb, LV_EVENT_GESTURE, nullptr);
        }
        for (auto &w : page.devices)
        {
            if (w.control)
            {
                lv_obj_add_event_cb(w.control, ui::toggle::switch_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
            }
        }
    }

    // Subscribe to entity updates and apply initial state
    {
        const auto &ents = state::entities();
        for (const auto &e : ents)
        {
            int id = state::subscribe_entity(e.id, &ui::rooms::on_entity_state_changed);
            if (id > 0)
                s_state_subscriptions.push_back(id);
        }
        // Apply current state to widgets (bootstrap after MQTT)
        for (const auto &e : ents)
        {
            ui::rooms::on_entity_state_changed(e);
        }
    }

    if (!s_room_pages.empty())
    {
        ui::rooms::show_initial_room();
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

    if (ui::screensaver::is_active())
    {
        if (!s_room_pages.empty())
        {
            int rooms = static_cast<int>(s_room_pages.size());
            if (s_current_room_index < 0 || s_current_room_index >= rooms)
            {
                s_current_room_index = 0;
            }
            ui::screensaver::hide_to_room(s_room_pages[s_current_room_index].root);
        }
        return;
    }

    switch (ev)
    {
    case 0: // next room
        ui::rooms::show_room_relative(+1, LV_SCREEN_LOAD_ANIM_MOVE_LEFT);
        break;
    case 1: // previous room
        ui::rooms::show_room_relative(-1, LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
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

    if (ui::screensaver::is_active())
    {
        if (!s_room_pages.empty())
        {
            int rooms = static_cast<int>(s_room_pages.size());
            if (s_current_room_index < 0 || s_current_room_index >= rooms)
            {
                s_current_room_index = 0;
            }
            ui::screensaver::hide_to_room(s_room_pages[s_current_room_index].root);
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
    std::string entity_id;

    if (!ui::rooms::get_current_entity_id(entity_id))
    {
        ESP_LOGW(TAG_UI, "No entity selected for toggle");
        return;
    }

    ui::toggle::trigger_toggle_for_entity(entity_id);
}

