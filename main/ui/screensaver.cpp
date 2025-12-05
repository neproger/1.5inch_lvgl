#include "screensaver.hpp"

#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "fonts.h"
#include "http_manager.hpp"
#include "icons.h"
#include "state_manager.hpp"
#include "devices_init.h"
#include "wifi_manager.h"
#include "app/app_state.hpp"
#include "app/app_events.hpp"
#include "app/app_config.hpp"
#include "locale_ru.hpp"

namespace ui
{
    namespace screensaver
    {
        lv_obj_t *s_screensaver_root = nullptr;
        lv_obj_t *s_weather_temp_label = nullptr;
        lv_obj_t *s_weather_cond_label = nullptr;
        lv_obj_t *s_weather_icon = nullptr;
        lv_obj_t *s_time_label = nullptr;
        lv_obj_t *s_date_label = nullptr;
        lv_obj_t *s_week_day_label = nullptr;

        static lv_timer_t *s_backlight_timer = nullptr;
        static lv_timer_t *s_clock_timer = nullptr;
        static bool s_active = false;
        static bool s_backlight_off = false;
        static esp_event_handler_instance_t s_app_state_handler = nullptr;
        static esp_event_handler_instance_t s_weather_handler = nullptr;
        static esp_event_handler_instance_t s_clock_handler = nullptr;

        static void backlight_timer_cb(lv_timer_t *timer);
        static void clock_timer_cb(lv_timer_t *timer);
        static void on_app_state_changed(const app_events::AppStateChangedPayload *payload);
        static void on_weather_updated();
        static void on_clock_updated();

        void ui_build_screensaver()
        {
            if (s_screensaver_root)
            {
                return;
            }

            s_screensaver_root = lv_obj_create(NULL);
            lv_obj_set_size(s_screensaver_root, LV_HOR_RES, LV_VER_RES);
            lv_obj_set_style_bg_color(s_screensaver_root, lv_color_hex(0x000000), 0);
            lv_obj_set_style_border_width(s_screensaver_root, 0, 0);
            lv_obj_remove_flag(s_screensaver_root, LV_OBJ_FLAG_SCROLLABLE);

            s_weather_icon = lv_image_create(s_screensaver_root);
            lv_image_set_src(s_weather_icon, &clear);
            lv_obj_set_style_img_recolor(s_weather_icon, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_img_recolor_opa(s_weather_icon, LV_OPA_COVER, 0);
            lv_obj_align(s_weather_icon, LV_ALIGN_CENTER, 0, -140);

            s_weather_temp_label = lv_label_create(s_screensaver_root);
            lv_label_set_text(s_weather_temp_label, "");
            lv_obj_set_style_text_color(s_weather_temp_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(s_weather_temp_label, &Montserrat_40, 0);
            lv_obj_align(s_weather_temp_label, LV_ALIGN_CENTER, 0, -70);

            s_weather_cond_label = lv_label_create(s_screensaver_root);
            lv_label_set_text(s_weather_cond_label, "");
            lv_obj_set_style_text_color(s_weather_cond_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(s_weather_cond_label, &Montserrat_30, 0);
            lv_obj_align(s_weather_cond_label, LV_ALIGN_CENTER, 0, -30);

            s_week_day_label = lv_label_create(s_screensaver_root);
            lv_label_set_text(s_week_day_label, "");
            lv_obj_set_style_text_color(s_week_day_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(s_week_day_label, &Montserrat_40, 0);
            lv_obj_align(s_week_day_label, LV_ALIGN_BOTTOM_MID, 0, -80);

            s_date_label = lv_label_create(s_screensaver_root);
            lv_label_set_text(s_date_label, "");
            lv_obj_set_style_text_color(s_date_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(s_date_label, &Montserrat_40, 0);
            lv_obj_align(s_date_label, LV_ALIGN_BOTTOM_MID, 0, -40);

            s_time_label = lv_label_create(s_screensaver_root);
            lv_label_set_text(s_time_label, "");
            lv_obj_set_style_text_color(s_time_label, lv_color_hex(0xFFFFFF), 0);
            lv_obj_set_style_text_font(s_time_label, &Montserrat_70, 0);
            lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, 20);
        }

        void init_support()
        {
            lvgl_port_lock(0);

            ui_build_screensaver();
            ui_update_weather_and_clock();

            if (s_app_state_handler == nullptr)
            {
                (void)esp_event_handler_instance_register(
                    APP_EVENTS,
                    app_events::APP_STATE_CHANGED,
                    [](void * /*arg*/, esp_event_base_t base, int32_t id, void *event_data)
                    {
                        if (base != APP_EVENTS || id != app_events::APP_STATE_CHANGED || !event_data)
                        {
                            return;
                        }
                        const auto *p = static_cast<const app_events::AppStateChangedPayload *>(event_data);
                        on_app_state_changed(p);
                    },
                    nullptr,
                    &s_app_state_handler);
            }

            if (s_weather_handler == nullptr)
            {
                (void)esp_event_handler_instance_register(
                    APP_EVENTS,
                    app_events::WEATHER_UPDATED,
                    [](void * /*arg*/, esp_event_base_t base, int32_t id, void * /*event_data*/)
                    {
                        if (base != APP_EVENTS || id != app_events::WEATHER_UPDATED)
                        {
                            return;
                        }
                        lvgl_port_lock(-1);
                        on_weather_updated();
                        lvgl_port_unlock();
                    },
                    nullptr,
                    &s_weather_handler);
            }

            if (s_clock_handler == nullptr)
            {
                (void)esp_event_handler_instance_register(
                    APP_EVENTS,
                    app_events::CLOCK_UPDATED,
                    [](void * /*arg*/, esp_event_base_t base, int32_t id, void * /*event_data*/)
                    {
                        if (base != APP_EVENTS || id != app_events::CLOCK_UPDATED)
                        {
                            return;
                        }
                        lvgl_port_lock(-1);
                        on_clock_updated();
                        lvgl_port_unlock();
                    },
                    nullptr,
                    &s_clock_handler);
            }

            if (s_clock_timer == nullptr)
            {
                s_clock_timer = lv_timer_create(clock_timer_cb, app_config::kScreensaverClockTickMs, nullptr);
            }

            lvgl_port_unlock();
        }

        bool is_active()
        {
            return s_active;
        }

        void show()
        {
            lvgl_port_lock(0);

            if (!s_screensaver_root)
            {
                ui_build_screensaver();
            }
            if (s_screensaver_root)
            {
                lv_disp_load_scr(s_screensaver_root);
                s_active = true;
                s_backlight_off = false;

                if (s_backlight_timer)
                {
                    lv_timer_del(s_backlight_timer);
                    s_backlight_timer = nullptr;
                }
                // After configured delay of screensaver activity, turn off the display
                s_backlight_timer = lv_timer_create(backlight_timer_cb, app_config::kScreensaverBacklightOffDelayMs, nullptr);
            }

            lvgl_port_unlock();
        }

        void ui_update_weather_and_clock()
        {
            on_weather_updated();
            on_clock_updated();
        }

        namespace
        {
            struct WeatherIconMapEntry
            {
                const char *condition;
                const lv_image_dsc_t *icon;
            };

            static const WeatherIconMapEntry kWeatherIconMap[] = {
                {"clear", &clear},
                {"clear-night", &clear_night},
                {"sunny", &sunny},
                {"partlycloudy", &partlycloudy},
                {"cloudy", &cloudy},
                {"overcast", &overcast},
                {"rainy", &rainy},
                {"pouring", &pouring},
                {"lightning", &lightning},
                {"lightning-rainy", &lightning_rainy},
                {"snowy", &snowy},
                {"snowy-rainy", &snowy_rainy},
                {"hail", &hail},
                {"fog", &fog},
                {"windy", &windy},
                {"windy-variant", &windy_variant},
                {"exceptional", &alien},
            };
        } // namespace

        static void on_weather_updated()
        {
            state::WeatherState w = state::weather();

            char temp_buf[32];
            const char *cond_text = nullptr;
            const lv_image_dsc_t *icon = &alien;

            if (w.valid)
            {
                std::snprintf(temp_buf, sizeof(temp_buf), "%.1f°C", w.temperature_c);

                const std::string &cond = w.condition;
                cond_text = locale_ru::weather_condition_to_text(cond);

                for (const auto &entry : kWeatherIconMap)
                {
                    if (cond == entry.condition)
                    {
                        icon = entry.icon;
                        break;
                    }
                }
            }
            else
            {
                std::snprintf(temp_buf, sizeof(temp_buf), "--.-°C");
                cond_text = "N/A";
                icon = &alien;
            }

            if (s_weather_temp_label)
                lv_label_set_text(s_weather_temp_label, temp_buf);
            if (s_weather_cond_label)
                lv_label_set_text(s_weather_cond_label, cond_text ? cond_text : "");
            if (s_weather_icon)
                lv_image_set_src(s_weather_icon, icon);
        }

        static void on_clock_updated()
        {
            state::ClockState c = state::clock();
            if (c.valid)
            {
                int64_t now_us = esp_timer_get_time();
                int64_t delta_sec = 0;
                if (now_us > c.sync_monotonic_us)
                {
                    delta_sec = (now_us - c.sync_monotonic_us) / 1000000;
                }
                int64_t total_sec = c.base_seconds + delta_sec;
                if (total_sec < 0)
                    total_sec = 0;
                int64_t sec_of_day = total_sec % 86400;
                int hour = static_cast<int>(sec_of_day / 3600);
                int minute = static_cast<int>((sec_of_day / 60) % 60);
                int second = static_cast<int>(sec_of_day % 60);

                int weekday = c.weekday;
                if (weekday < 0 || weekday > 6)
                    weekday = 0;

                int month = c.month;
                if (month < 1 || month > 12)
                    month = 1;

                int day = c.day;
                if (day < 1)
                    day = 1;

                char time_buf[32];
                char date_buf[32];

                std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d", hour, minute, second);
                std::snprintf(date_buf, sizeof(date_buf), "%s, %d", locale_ru::kMonthNames[month], day);

                if (s_time_label)
                    lv_label_set_text(s_time_label, time_buf);
                if (s_date_label)
                    lv_label_set_text(s_date_label, date_buf);
                if (s_week_day_label)
                    lv_label_set_text(s_week_day_label, locale_ru::kWeekdayNames[weekday]);
            }
        }

        static const char *TAG = "screensaver";

        static void backlight_timer_cb(lv_timer_t *timer)
        {
            (void)timer;
            if (!s_active || s_backlight_off)
            {
                return;
            }

            ESP_LOGI(TAG, "Screensaver: turning display off after idle");
            if (devices_display_set_enabled(false) == ESP_OK)
            {
                s_backlight_off = true;
            }
        }

        static void clock_timer_cb(lv_timer_t *timer)
        {
            (void)timer;
            lvgl_port_lock(-1);
            on_clock_updated();
            lvgl_port_unlock();
        }

        static void on_app_state_changed(const app_events::AppStateChangedPayload *payload)
        {
            if (!payload)
            {
                return;
            }

            AppState new_state = static_cast<AppState>(payload->new_state);
            switch (new_state)
            {
            case AppState::NormalScreensaver:
                show();
                break;
            default:
                if (s_backlight_timer)
                {
                    lv_timer_del(s_backlight_timer);
                    s_backlight_timer = nullptr;
                }
                if (s_backlight_off)
                {
                    (void)devices_display_set_enabled(true);
                    s_backlight_off = false;
                }
                s_active = false;
                break;
            }
        }

    } // namespace screensaver
} // namespace ui
