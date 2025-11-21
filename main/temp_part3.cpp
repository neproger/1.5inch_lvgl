                lv_label_set_text(page.title_label, page.area_name.c_str());
                lv_obj_set_style_text_color(page.title_label, lv_color_hex(0xE6E6E6), 0);
                lv_obj_set_style_text_font(page.title_label, &Montserrat_50, 0);
                lv_obj_align(page.title_label, LV_ALIGN_TOP_MID, 0, 30);

                // Контент
                page.list_container = lv_obj_create(page.root);
                lv_obj_set_style_bg_opa(page.list_container, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(page.list_container, 0, 0);
                lv_obj_set_style_pad_row(page.list_container, 20, 0); // вертикальный gap между элементами внутри строки

                // занимаем всё под заголовком
                int top = 80; // отступ под title
                lv_obj_set_size(page.list_container, LV_PCT(90), LV_VER_RES - top);
                lv_obj_align(page.list_container, LV_ALIGN_TOP_MID, 0, top);

                // включаем вертикальный скролл и flex-колонку
                lv_obj_set_scroll_dir(page.list_container, LV_DIR_VER);
                lv_obj_set_scrollbar_mode(page.list_container, LV_SCROLLBAR_MODE_AUTO);
                lv_obj_set_flex_flow(page.list_container, LV_FLEX_FLOW_COLUMN);
                lv_obj_set_flex_align(page.list_container,
                                      LV_FLEX_ALIGN_START, // main axis
                                      LV_FLEX_ALIGN_START, // cross axis
                                      LV_FLEX_ALIGN_START);

                // Добавляем виджеты устройств для этой комнаты
                for (size_t i = 0; i < entities.size(); ++i)
                {
                    const auto &ent = entities[i];
                    if (ent.area_id != area.id)
                        continue;

                    // Пока все сущности отображаем как переключатели.
                    // В будущем можно выбирать тип виджета по типу устройства.
                    ui_add_switch_widget(page, ent);
                }

                s_room_pages.push_back(std::move(page));
            }

            for (auto &page : s_room_pages)
            {
                for (auto &w : page.devices)
                {
                    if (w.control)
                    {
                        lv_obj_add_event_cb(w.control, switch_event_cb, LV_EVENT_VALUE_CHANGED, nullptr);
                    }
                }
            }

            ui_update_weather_label();
        }

        static void ui_update_weather_label()
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
                    cond_text = "Ясно";
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
                    cond_text = "Гроза и дождь";
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
                    cond_text = "Ветрено";
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
                    cond_text = w.condition.c_str();
                    icon = &alien;
                }
            }
            else
            {
                std::snprintf(temp_buf, sizeof(temp_buf), "--°C");
                cond_text = "--";
                icon = &alien;
            }

            for (auto &page : s_room_pages)
            {
                if (page.weather_label)
                {
                    lv_label_set_text(page.weather_label, temp_buf);
                }
            }

            if (s_weather_temp_label)
            {
                lv_label_set_text(s_weather_temp_label, temp_buf);
            }
            if (s_weather_cond_label)
            {
                lv_label_set_text(s_weather_cond_label, cond_text ? cond_text : "");
            }
            if (s_weather_icon && icon)
            {
                lv_image_set_src(s_weather_icon, icon);
            }

            // Update clock label based on ClockState
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

    } // namespace rooms
} // namespace ui

// Legacy overlay labels (status/info) removed in new UI:
// each entity is represented by its own widget on the room page.
