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

enum class UiMode
{
    Rooms,
    Screensaver
};

static UiMode s_ui_mode = UiMode::Rooms;
static lv_obj_t *s_screensaver_root = NULL;
static lv_obj_t *s_weather_temp_label = NULL;
static lv_obj_t *s_weather_cond_label = NULL;
static lv_timer_t *s_idle_timer = NULL;
static const uint32_t kScreensaverTimeoutMs = 5000;

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
        lv_obj_t *weather_label = nullptr;
        std::vector<DeviceWidget> devices;
    };

    static std::vector<RoomPage> s_room_pages;
    static int s_current_room_index = 0;
    static int s_current_device_index = 0;

    static void ui_build_room_pages();
    static void ui_update_weather_label();
}

static void handle_single_click(void);
static void ha_toggle_task(void *arg);
static void trigger_toggle_for_entity(const std::string &entity_id);
static void switch_event_cb(lv_event_t *e);
static void on_state_entity_changed(const state::Entity &e);
static void ui_show_spinner(void);
static void ui_hide_spinner(void);
static void on_weather_updated_from_http(void);
static void ui_build_screensaver(void);
static void idle_timer_cb(lv_timer_t *timer);
void ui_app_init(void)
{
    // Время последнего ввода (как у тебя было)
    s_last_input_us = esp_timer_get_time();
    ui_build_room_pages();

    // стартовый экран и стартовый элемент
    if (!s_room_pages.empty())
    {
        s_current_room_index = 0;
        s_current_device_index = 0;
        lv_disp_load_scr(s_room_pages[0].root);
    }

    // 3. Таймер для UI — оставляем твой код
    {
        const auto &ents = state::entities();
        for (const auto &e : ents)
        {
            int id = state::subscribe_entity(e.id, &on_state_entity_changed);
            if (id > 0)
                s_state_subscriptions.push_back(id);
        }
        // Синхронизируем UI с уже известным состоянием (после bootstrap и MQTT)
        for (const auto &e : ents) {
            on_state_entity_changed(e);
        }
    }
}

extern "C" void ui_init_screensaver_support(void)
{
    ui_build_screensaver();
    ui_update_weather_label();

    if (s_idle_timer == NULL)
    {
        s_idle_timer = lv_timer_create(idle_timer_cb, 500, NULL);
    }
}

extern "C" void ui_start_weather_polling(void)
{
    http_manager::start_weather_polling(&on_weather_updated_from_http);
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
    case 0:
        if (!s_room_pages.empty())
        {
            int rooms = static_cast<int>(s_room_pages.size());
            s_current_room_index = (s_current_room_index + 1) % rooms;
            s_current_device_index = 0;
            lv_disp_load_scr(s_room_pages[s_current_room_index].root);
        }
        break;
    case 1:
        if (!s_room_pages.empty())
        {
            int rooms = static_cast<int>(s_room_pages.size());
            s_current_room_index = (s_current_room_index - 1 + rooms) % rooms;
            s_current_device_index = 0;
            lv_disp_load_scr(s_room_pages[s_current_room_index].root);
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

static void on_state_entity_changed(const state::Entity &e)
{
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
}

static void ui_build_screensaver(void)
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

    s_weather_temp_label = lv_label_create(s_screensaver_root);
    lv_label_set_text(s_weather_temp_label, "");
    lv_obj_set_style_text_color(s_weather_temp_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_weather_temp_label, &Montserrat_70, 0);
    lv_obj_align(s_weather_temp_label, LV_ALIGN_CENTER, 0, -30);

    s_weather_cond_label = lv_label_create(s_screensaver_root);
    lv_label_set_text(s_weather_cond_label, "");
    lv_obj_set_style_text_color(s_weather_cond_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(s_weather_cond_label, &Montserrat_40, 0);
    lv_obj_align(s_weather_cond_label, LV_ALIGN_CENTER, 0, 40);
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
        if (inactive_ms >= kScreensaverTimeoutMs && s_screensaver_root)
        {
            lv_disp_load_scr(s_screensaver_root);
            s_ui_mode = UiMode::Screensaver;
        }
    }
}

static void on_weather_updated_from_http(void)
{
    lvgl_port_lock(-1);
    ui_update_weather_label();
    lvgl_port_unlock();
}

namespace // room pages impl
{
    static void ui_add_switch_widget(RoomPage &page, const state::Entity &ent)
    {
        DeviceWidget w;
        w.entity_id = ent.id;
        w.name = ent.name;
        w.container = lv_obj_create(page.list_container);
        lv_obj_set_style_bg_opa(w.container, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(w.container, 0, 0);
        lv_obj_set_width(w.container, LV_PCT(100));
        lv_obj_set_height(w.container, LV_SIZE_CONTENT); // важное место
        lv_obj_set_style_pad_ver(w.container, 8, 0);     // вертикальные отступы
        lv_obj_set_style_pad_hor(w.container, 8, 0);     // горизонтальные отступы
        lv_obj_set_style_pad_row(w.container, 30, 0);    // вертикальный gap между элементами внутри строки

        // lv_obj_set_style_pad_row(w.container, 12, 0);
        lv_obj_remove_flag(w.container, LV_OBJ_FLAG_SCROLLABLE);
        // высота авто по содержимому
        lv_obj_set_flex_flow(w.container, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            w.container,
            LV_FLEX_ALIGN_CENTER, // горизонталь: центр
            LV_FLEX_ALIGN_CENTER, // вертикаль: центр
            LV_FLEX_ALIGN_CENTER);

        w.label = lv_label_create(w.container);
        lv_label_set_text(w.label, ent.name.c_str());
        lv_obj_set_style_text_color(w.label, lv_color_hex(0xE6E6E6), 0);
        lv_obj_set_style_text_font(w.label, &Montserrat_40, 0);

        // Switch control for binary entities (initial state from current value)
        w.control = lv_switch_create(w.container);
        // вертикальная ориентация
        lv_switch_set_orientation(w.control, LV_SWITCH_ORIENTATION_VERTICAL);
        lv_obj_set_style_width(w.control, 100, LV_PART_MAIN);
        lv_obj_set_style_height(w.control, 160, LV_PART_MAIN);

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
            lv_obj_set_size(page.root, LV_HOR_RES, LV_VER_RES);
            lv_obj_set_style_bg_color(page.root, lv_color_hex(0x000000), 0);
            lv_obj_set_style_border_width(page.root, 0, 0);
            lv_obj_remove_flag(page.root, LV_OBJ_FLAG_SCROLLABLE);

            page.title_label = lv_label_create(page.root);
            lv_label_set_text(page.title_label, page.area_name.c_str());
            lv_obj_set_style_text_color(page.title_label, lv_color_hex(0xE6E6E6), 0);
            lv_obj_set_style_text_font(page.title_label, &Montserrat_20, 0);
            lv_obj_align(page.title_label, LV_ALIGN_TOP_MID, 0, 40);

            // list_container – только он скроллится
            page.list_container = lv_obj_create(page.root);
            lv_obj_set_style_bg_opa(page.list_container, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(page.list_container, 0, 0);

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
        char cond_buf[64];

        if (w.valid)
        {
            std::snprintf(temp_buf, sizeof(temp_buf), "%.1f°C", w.temperature_c);
            std::snprintf(cond_buf, sizeof(cond_buf), "%s", w.condition.c_str());
        }
        else
        {
            std::snprintf(temp_buf, sizeof(temp_buf), "--°C");
            std::snprintf(cond_buf, sizeof(cond_buf), "--");
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
            lv_label_set_text(s_weather_cond_label, cond_buf);
        }
    }

} // namespace

// Legacy overlay labels (status/info) removed in new UI:
// each entity is represented by its own widget on the room page.
