#include "ui_app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "fonts.h"
#include "app/router.hpp"
#include "core/store.hpp"
#include "app/entities.hpp"
#include "devices_init.h"
#include "wifi_manager.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG_UI = "UI";
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_status_ring = NULL;
static lv_obj_t *s_info_label = NULL; /* Secondary info/description label */
static lv_obj_t *s_background = NULL;
static TaskHandle_t s_ha_req_task = NULL;
static esp_timer_handle_t s_ha_ui_timer = NULL; /* periodic UI updater */
static bool s_last_online = false;
static int64_t s_last_toggle_us = 0; /* simple cooldown to avoid hammering */

static const int s_ui_items_count = app::g_entity_count;
static int s_cur_item = 0;
static volatile bool s_state_dirty = true;

static bool s_screensaver = false;
static int64_t s_last_input_us = 0;
static const int64_t SCREENSAVER_TIMEOUT_US = 30LL * 1000 * 1000; /* 30 s */

// Draw circular border positioned by center (cx, cy) and radius.
static lv_obj_t *create_perimeter_ring(lv_obj_t *parent, int cx, int cy, int radius, int n, int d, lv_color_t color);
static void handle_single_click(void);
static void ha_ui_timer_cb(void *arg);
static void ha_toggle_task(void *arg);
static void ui_show_current_item(void);
static void ui_enter_screensaver(void);
static void ui_exit_screensaver(void);
static void ui_refresh_current_state(void);
static void store_listener(const core::AppState &st);
void setInfo(const char *text);
void setLabel(const char *text);

namespace // class UiScreen
{

    enum class UiScreen
    {
        Kitchen,
        Hallway,
        ScreenSaver
    };

    static UiScreen s_current_screen = UiScreen::ScreenSaver;

    static lv_obj_t *s_kitchen_scr = nullptr;
    static lv_obj_t *s_hallway_scr = nullptr;
    static lv_obj_t *s_screen_saver_scr = nullptr;

    // Виджеты для Kitchen
    static lv_obj_t *s_kitchen_title = nullptr;
    static lv_obj_t *s_kitchen_state_label = nullptr;

    // Виджеты для Hallway
    static lv_obj_t *s_hallway_title = nullptr;
    static lv_obj_t *s_hallway_state_label = nullptr;

    // Виджеты для ScreenSaver
    static lv_obj_t *s_ss_title = nullptr;

    // Если у тебя где-то был общий статус-обруч — можешь
    static lv_obj_t *ring = nullptr;
} // namespace

namespace // ui_show_screen
{

    void ui_show_screen(UiScreen scr)
    {
        if (scr == s_current_screen)
        {
            return;
        }
        s_current_screen = scr;

        switch (scr)
        {
        case UiScreen::Kitchen:
            if (s_kitchen_scr)
            {
                lv_screen_load(s_kitchen_scr);
            }
            break;
        case UiScreen::Hallway:
            if (s_hallway_scr)
            {
                lv_screen_load(s_hallway_scr);
            }
            break;
        case UiScreen::ScreenSaver:
            if (s_screen_saver_scr)
            {
                lv_screen_load(s_screen_saver_scr);
            }
            break;
        }
    }

} // namespace

namespace // ui_build_kitchen_screen
{
    static void ui_build_kitchen_screen()
    {
        lv_obj_t *scr = lv_obj_create(NULL);
        s_kitchen_scr = scr;

        // Заголовок "Kitchen"
        s_kitchen_title = lv_label_create(scr);
        lv_label_set_text(s_kitchen_title, "Kitchen");
        lv_obj_set_style_text_color(s_kitchen_title, lv_color_hex(0xE6E6E6), 0);
        lv_obj_set_style_text_font(s_kitchen_title, &Montserrat_20, 0);
        lv_obj_align(s_kitchen_title, LV_ALIGN_TOP_MID, 0, 40);

        // Статус девайса/группы (пока заглушка)
        s_kitchen_state_label = lv_label_create(scr);
        lv_label_set_text(s_kitchen_state_label, "Кухня");
        lv_obj_set_style_text_color(s_kitchen_state_label, lv_color_hex(0xCCCCCC), 0);
        lv_obj_align(s_kitchen_state_label, LV_ALIGN_BOTTOM_MID, 0, -40);

        // Если хочешь, можно сюда же перенести твой perimeter ring:
        /*
        int w = lv_obj_get_width(s_kitchen_scr);
        int h = lv_obj_get_height(s_kitchen_scr);
        int margin = 1;
        int r = (w < h ? w : h) / 2 - margin;
        if (r < 4)
            r = (w < h ? w : h) / 2 - 1;
        s_status_ring = create_perimeter_ring(s_kitchen_scr, w / 2, h / 2, r, 0, 2, lv_color_hex(0xFF0000));
        */
    }

} // namespace

namespace // ui_build_hallway_screen
{

    static void ui_build_hallway_screen()
    {
        lv_obj_t *scr = lv_obj_create(NULL);
        s_hallway_scr = scr;

        s_hallway_title = lv_label_create(scr);
        lv_label_set_text(s_hallway_title, "Hallway");
        lv_obj_set_style_text_color(s_hallway_title, lv_color_hex(0xE6E6E6), 0);
        lv_obj_set_style_text_font(s_hallway_title, &Montserrat_20, 0);
        lv_obj_align(s_hallway_title, LV_ALIGN_TOP_MID, 0, 40);

        s_hallway_state_label = lv_label_create(scr);
        lv_label_set_text(s_hallway_state_label, "Холл");
        lv_obj_set_style_text_color(s_hallway_state_label, lv_color_hex(0xCCCCCC), 0);
        lv_obj_align(s_hallway_state_label, LV_ALIGN_BOTTOM_MID, 0, -40);
    }

} // namespace

namespace // ui_build_screensaver_screen
{

    static void ui_build_screensaver_screen()
    {
        lv_obj_t *scr = lv_obj_create(NULL);
        s_screen_saver_scr = scr;

        s_ss_title = lv_label_create(scr);
        lv_label_set_text(s_ss_title, "KazDEV");
        lv_obj_set_style_text_color(s_ss_title, lv_color_hex(0x404040), 0);
        lv_obj_set_style_text_font(s_ss_title, &Montserrat_20, 0);
        lv_obj_align(s_ss_title, LV_ALIGN_CENTER, 0, 0);
    }

} // namespace

namespace // update_state
{

    void ui_update_kitchen_state(const char *state_text)
    {
        if (s_kitchen_state_label)
        {
            lv_label_set_text_fmt(s_kitchen_state_label, "State: %s", state_text);
        }
    }

    void ui_update_hallway_state(const char *state_text)
    {
        if (s_hallway_state_label)
        {
            lv_label_set_text_fmt(s_hallway_state_label, "State: %s", state_text);
        }
    }

} // namespace

void ui_app_init(void)
{
    // Время последнего ввода (как у тебя было)
    s_last_input_us = esp_timer_get_time();

    if (s_background == NULL)
    {
        lv_disp_t *disp = lv_disp_get_default();
        lv_obj_t *layer = lv_layer_bottom();
        if (disp && layer)
        {
            s_background = lv_obj_create(layer);
            lv_coord_t w = lv_disp_get_hor_res(disp);
            lv_coord_t h = lv_disp_get_ver_res(disp);
            lv_obj_set_size(s_background, w, h);
            lv_obj_clear_flag(s_background, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_style_bg_color(s_background, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(s_background, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
        }
    }

    // 1. Построить все скрины
    ui_build_kitchen_screen();
    ui_build_hallway_screen();
    ui_build_screensaver_screen();

    // 2. Показать стартовый (можно ScreenSaver или Kitchen)
    ui_show_screen(UiScreen::ScreenSaver);
    // или:
    // ui_show_screen(UiScreen::Kitchen);

    // 3. Таймер для UI — оставляем твой код
    if (s_ha_ui_timer == NULL)
    {
        esp_timer_create_args_t args = {};
        args.callback = &ha_ui_timer_cb;
        args.name = "ui_ha_upd";
        esp_timer_create(&args, &s_ha_ui_timer);
        esp_timer_start_periodic(s_ha_ui_timer, 500 * 1000); /* 0.5s UI refresh */
    }

    // 4. Подписка на store
    core::store_subscribe(&store_listener);
    core::store_set_selected(s_cur_item);

    // 5. Первичная отрисовка данных из store (потом разнесём по экранам)
    ui_refresh_current_state();
}

static void ha_ui_timer_cb(void *arg)
{
    (void)arg;
    const core::AppState &st = core::store_get_state();
    bool online = st.connected;
    if (!s_screensaver && s_state_dirty)
    {
        s_state_dirty = false;
        char info[32];
        if (s_cur_item < st.entity_count)
        {
            strncpy(info, st.entities[s_cur_item].value, sizeof(info) - 1);
            info[sizeof(info) - 1] = '\0';
            setInfo(info);
        }
        else
        {
            setInfo("-");
        }
    }
    if (!s_screensaver && online != s_last_online && s_status_ring)
    {
        lvgl_port_lock(-1);
        lv_color_t col = online ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000);
        lv_obj_set_style_border_color(s_status_ring, col, 0);
        lvgl_port_unlock();
        s_last_online = online;
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
    if (s_screensaver)
    {
        ui_exit_screensaver();
        return;
    }
    switch (ev)
    {
    case 0:
        s_cur_item = (s_cur_item + 1) % s_ui_items_count;
        core::store_set_selected(s_cur_item);
        ui_show_current_item();
        break;
    case 1:
        s_cur_item = (s_cur_item - 1 + s_ui_items_count) % s_ui_items_count;
        core::store_set_selected(s_cur_item);
        ui_show_current_item();
        break;
    default:
        break;
    }
    ESP_LOGI(TAG_UI, "%s | sel=%d:%s", knob_event_table[ev], s_cur_item, app::g_entities[s_cur_item].name);
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
    if (s_screensaver)
    {
        ui_exit_screensaver();
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
    case 5:
        ESP_LOGI(TAG_UI, "%s", label);
        setLabel(label);
        break;
    case 7:
        ESP_LOGI(TAG_UI, "%s", label);
        setLabel(label);
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
        setLabel("WAIT");
        return;
    }
    if (!router::is_connected())
    {
        setLabel("No MQTT");
        return;
    }
    if (s_ha_req_task == NULL)
    {
        BaseType_t ok = xTaskCreate(ha_toggle_task, "ha_toggle", 4096, NULL, 4, &s_ha_req_task);
        if (ok != pdPASS)
        {
            ESP_LOGW(TAG_UI, "Failed to create ha_toggle task");
        }
    }
}

static void ha_toggle_task(void *arg)
{
    (void)arg;
    // setInfo("...");
    if (!wifi_manager_is_connected())
    {
        setInfo("No WiFi");
        s_ha_req_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    const char *entity = app::g_entities[s_cur_item].entity_id;
    esp_err_t err = router::toggle(entity);
    if (err == ESP_OK)
    {
        s_last_toggle_us = esp_timer_get_time();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    else
    {
        setInfo("MQTT ERR");
    }
    s_ha_req_task = NULL;
    vTaskDelete(NULL);
}

static void ui_show_current_item(void)
{
    setLabel(app::g_entities[s_cur_item].name);
    ui_refresh_current_state();
}

static void ui_enter_screensaver(void)
{
    s_screensaver = true;
    devices_set_backlight_percent(10);
    lvgl_port_lock(-1);
    if (s_status_label)
    {
        lv_label_set_text(s_status_label, "");
    }
    lvgl_port_unlock();
}

static void ui_exit_screensaver(void)
{
    s_screensaver = false;
    devices_set_backlight_percent(90);
    lvgl_port_lock(-1);
    if (s_status_ring)
    {
        lv_obj_set_style_border_width(s_status_ring, 2, 0);
        lv_color_t col = s_last_online ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000);
        lv_obj_set_style_border_color(s_status_ring, col, 0);
    }
    lvgl_port_unlock();
    ui_show_current_item();
    s_last_input_us = esp_timer_get_time();
}

static lv_obj_t *create_perimeter_ring(lv_obj_t *parent, int cx, int cy, int radius, int n, int d, lv_color_t color)
{
    (void)n; /* not used */
    int border_w = (d > 0) ? d : 2;
    if (radius < 2)
        radius = 2;
    int side = radius * 2;
    lv_obj_t *ring = lv_obj_create(parent);
    lv_obj_remove_style_all(ring);
    lv_obj_set_size(ring, side, side);
    int parent_w = lv_obj_get_width(parent);
    int parent_h = lv_obj_get_height(parent);
    int off_x = cx - parent_w / 2;
    int off_y = cy - parent_h / 2;
    lv_obj_align(ring, LV_ALIGN_CENTER, off_x, off_y);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring, border_w, 0);
    lv_obj_set_style_border_color(ring, color, 0);
    return ring;
}

static void ui_refresh_current_state(void)
{
    const core::AppState &st = core::store_get_state();
    if (s_cur_item < st.entity_count)
    {
        const char *val = st.entities[s_cur_item].value;
        setInfo(val && val[0] ? val : "-");
    }
    else
    {
        setInfo("-");
    }
}

static void store_listener(const core::AppState &st)
{
    (void)st;
    s_state_dirty = true;
}

extern "C" void setLabel(const char *text)
{
    if (!text)
        text = "";
    lvgl_port_lock(-1);
    if (s_status_label == NULL)
    {
        lv_obj_t *scr = lv_screen_active();
        s_status_label = lv_label_create(scr);
        lv_obj_set_style_text_font(s_status_label, &Montserrat_40, 0);
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xE6E6E6), 0);
        lv_obj_center(s_status_label);
    }
    lv_label_set_text(s_status_label, text);
    lvgl_port_unlock();
}

void setInfo(const char *text)
{
    if (!text)
        text = "";
    lvgl_port_lock(-1);
    if (s_info_label == NULL)
    {
        lv_obj_t *scr = lv_screen_active();
        s_info_label = lv_label_create(scr);
        lv_obj_set_style_text_font(s_info_label, &Montserrat_40, 0);
        lv_obj_set_style_text_color(s_info_label, lv_color_hex(0xA0A0A0), 0);
        if (s_status_label)
        {
            lv_obj_align_to(s_info_label, s_status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
        }
        else
        {
            lv_obj_center(s_info_label);
        }
    }
    lv_label_set_text(s_info_label, text);
    if (s_status_label)
    {
        lv_obj_align_to(s_info_label, s_status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    }
    lvgl_port_unlock();
}
