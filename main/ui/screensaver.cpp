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
        static void enter_light_sleep(void);
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
                s_clock_timer = lv_timer_create(clock_timer_cb, 1000, nullptr);
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
                // After 5 seconds of screensaver activity, turn off the display
                s_backlight_timer = lv_timer_create(backlight_timer_cb, 5000, nullptr);
            }

            lvgl_port_unlock();
        }

        void ui_update_weather_and_clock()
        {
            on_weather_updated();
            on_clock_updated();
        }

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
                if (cond == "clear")
                {
                    cond_text = "Ясно";
                    icon = &clear;
                }
                else if (cond == "clear-night")
                {
                    cond_text = "Ясно (ночь)";
                    icon = &clear_night;
                }
                else if (cond == "sunny")
                {
                    cond_text = "Солнечно";
                    icon = &sunny;
                }
                else if (cond == "partlycloudy")
                {
                    cond_text = "Переменная облачность";
                    icon = &partlycloudy;
                }
                else if (cond == "cloudy")
                {
                    cond_text = "Облачно";
                    icon = &cloudy;
                }
                else if (cond == "overcast")
                {
                    cond_text = "Пасмурно";
                    icon = &overcast;
                }
                else if (cond == "rainy")
                {
                    cond_text = "Дождь";
                    icon = &rainy;
                }
                else if (cond == "pouring")
                {
                    cond_text = "Ливень";
                    icon = &pouring;
                }
                else if (cond == "lightning")
                {
                    cond_text = "Гроза";
                    icon = &lightning;
                }
                else if (cond == "lightning-rainy")
                {
                    cond_text = "Гроза с дождём";
                    icon = &lightning_rainy;
                }
                else if (cond == "snowy")
                {
                    cond_text = "Снег";
                    icon = &snowy;
                }
                else if (cond == "snowy-rainy")
                {
                    cond_text = "Снег с дождём";
                    icon = &snowy_rainy;
                }
                else if (cond == "hail")
                {
                    cond_text = "Град";
                    icon = &hail;
                }
                else if (cond == "fog")
                {
                    cond_text = "Туман";
                    icon = &fog;
                }
                else if (cond == "windy")
                {
                    cond_text = "Ветер";
                    icon = &windy;
                }
                else if (cond == "windy-variant")
                {
                    cond_text = "Ветрено, переменная облачность";
                    icon = &windy_variant;
                }
                else if (cond == "exceptional")
                {
                    cond_text = "Необычная погода";
                    icon = &alien;
                }
                else
                {
                    cond_text = cond.c_str();
                    icon = &alien;
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

                static const char *kWeekdayNames[7] = {
                    "Понедельник",
                    "Вторник",
                    "Среда",
                    "Четверг",
                    "Пятница",
                    "Суббота",
                    "Воскресенье"};

                static const char *kMonthNames[13] = {
                    "",
                    "Январь",
                    "Февраль",
                    "Март",
                    "Апрель",
                    "Май",
                    "Июнь",
                    "Июль",
                    "Август",
                    "Сентябрь",
                    "Октябрь",
                    "Ноябрь",
                    "Декабрь"};

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
                std::snprintf(date_buf, sizeof(date_buf), "%s, %d", kMonthNames[month], day);

                if (s_time_label)
                    lv_label_set_text(s_time_label, time_buf);
                if (s_date_label)
                    lv_label_set_text(s_date_label, date_buf);
                if (s_week_day_label)
                    lv_label_set_text(s_week_day_label, kWeekdayNames[weekday]);
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

        static void enter_light_sleep(void)
        {
            ESP_LOGI(TAG, "Entering light sleep...");
            gpio_config_t config = {};
            config.pin_bit_mask = (1ULL << BSP_BTN_PRESS);
            config.mode = GPIO_MODE_INPUT;
            config.pull_up_en = GPIO_PULLUP_ENABLE;
            config.pull_down_en = GPIO_PULLDOWN_DISABLE;
            config.intr_type = GPIO_INTR_DISABLE;
            gpio_config(&config);
            gpio_wakeup_enable(BSP_BTN_PRESS, GPIO_INTR_LOW_LEVEL);
            esp_sleep_enable_gpio_wakeup();

            esp_err_t err = esp_light_sleep_start();
            esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
            ESP_LOGI(TAG, "Light sleep returned: err=%s, cause=%d", esp_err_to_name(err), (int)cause);
            lv_display_trigger_activity(nullptr);
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
