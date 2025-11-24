#include "ui_app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "state_manager.hpp"
#include "rooms.hpp"
#include "screensaver.hpp"
#include "switch.hpp"
#include "app/app_events.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

static const char *TAG_UI = "UI";

static std::vector<int> s_state_subscriptions;

using ui::rooms::DeviceWidget;
using ui::rooms::RoomPage;
using ui::rooms::s_current_device_index;
using ui::rooms::s_current_room_index;
using ui::rooms::s_room_pages;
using ui::rooms::ui_build_room_pages;
using ui::screensaver::s_screensaver_root;

static void root_input_cb(lv_event_t *e);

void ui_app_init(void)
{
    // Build UI room pages
    ui_build_room_pages();

    // Attach gesture and switch callbacks
    for (auto &page : s_room_pages)
    {
        if (page.root)
        {
            lv_obj_add_event_cb(page.root, root_input_cb, LV_EVENT_GESTURE, nullptr);
        }
        for (auto &w : page.devices)
        {
            if (w.control)
            {
                lv_obj_add_event_cb(w.control, ui::toggle::switch_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
            }
        }
    }

    // Initialize toggle handling (event bus subscriptions)
    (void)ui::toggle::init();

    // Attach root input handler to screensaver root for press events
    if (s_screensaver_root)
    {
        lv_obj_add_event_cb(s_screensaver_root, root_input_cb, LV_EVENT_PRESSED, nullptr);
    }

    // Apply current state to widgets (bootstrap after MQTT)
    {
        const auto &ents = state::entities();
        for (const auto &e : ents)
        {
            ui::rooms::on_entity_state_changed(e);
        }
    }

    if (!s_room_pages.empty())
    {
        ui::rooms::show_initial_room();
    }

    // Optional: additional UI-level subscriptions can be added here later
}

static void root_input_cb(lv_event_t *e)
{
    if (!e)
    {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);
    std::int64_t now_us = esp_timer_get_time();

    if (code == LV_EVENT_GESTURE)
    {
        lv_indev_t *indev = lv_indev_get_act();
        if (!indev)
        {
            return;
        }

        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        int gesture_code = -1;
        if (dir == LV_DIR_LEFT)
        {
            gesture_code = static_cast<int>(app_events::GestureCode::SwipeLeft);
        }
        else if (dir == LV_DIR_RIGHT)
        {
            gesture_code = static_cast<int>(app_events::GestureCode::SwipeRight);
        }

        if (gesture_code >= 0)
        {
            (void)app_events::post_gesture(gesture_code, now_us, false);
        }
    }
    else if (code == LV_EVENT_PRESSED)
    {
        lv_obj_t *target = static_cast<lv_obj_t *>(lv_event_get_target(e));
        if (target == s_screensaver_root)
        {
            (void)app_events::post_wake_screensaver(now_us, false);
        }
    }
}

// Currently no UI-level event handlers; high-level events are handled
// by their respective domains (screensaver, rooms, input_controller).

