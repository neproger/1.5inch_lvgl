#include "ui_app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "fonts.h"
#include "app/router.hpp"
#include "config/config.hpp"
#include "core/store.hpp"
#include "infra/device/devices_init.h"
#include "infra/network/wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <cstdint>
#include <cstring>
#include <cmath>

static const char *TAG_UI = "UI";
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_status_ring = NULL;
static lv_obj_t *s_info_label = NULL; /* Secondary info/description label */
static esp_timer_handle_t s_ha_ui_timer = NULL; /* periodic UI updater */
static bool s_last_online = false;
static int64_t s_last_toggle_us = 0; /* simple cooldown to avoid hammering */

static constexpr int kMaxUiEntities = config::kMaxEntities;
static int s_ui_items_count = 0;
static int s_cur_item = 0;
static const config::HaSettings *s_cfg = nullptr;

static bool s_screensaver = false;
static int64_t s_last_input_us = 0;
static const int64_t SCREENSAVER_TIMEOUT_US = 30LL * 1000 * 1000; /* 30 s */

static portMUX_TYPE s_state_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_store_dirty = false;
static bool s_store_connected = false;
static char s_item_state[kMaxUiEntities][core::kEntityValueLen] = {};

// Draw circular border positioned by center (cx, cy) and radius.
static lv_obj_t *create_perimeter_ring(lv_obj_t *parent, int cx, int cy, int radius, int n, int d, lv_color_t color);
static void handle_single_click(void);
static void ha_ui_timer_cb(void *arg);
static void ui_show_current_item(void);
static void ui_enter_screensaver(void);
static void ui_exit_screensaver(void);
static void init_state_defaults(void);
static const config::Entity *get_entity(int index);
void setInfo(const char *text);
void setLabel(const char *text);

static void init_state_defaults(void)
{
    portENTER_CRITICAL(&s_state_mux);
    for (int i = 0; i < kMaxUiEntities; ++i)
    {
        s_item_state[i][0] = '-';
        s_item_state[i][1] = '\0';
    }
    s_store_connected = false;
    s_store_dirty = true;
    portEXIT_CRITICAL(&s_state_mux);
}

static void copy_entity_value(int index, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0)
        return;
    portENTER_CRITICAL(&s_state_mux);
    if (index >= 0 && index < s_ui_items_count)
    {
        strncpy(dst, s_item_state[index], dst_len - 1);
        dst[dst_len - 1] = '\0';
    }
    else
    {
        dst[0] = '\0';
    }
    portEXIT_CRITICAL(&s_state_mux);
}

static const config::Entity *get_entity(int index)
{
    if (!s_cfg)
        return nullptr;
    if (index < 0 || index >= s_ui_items_count)
        return nullptr;
    return &s_cfg->entities[index];
}

extern "C" void ui_app_init(void)
{
    if (config::load() != ESP_OK)
    {
        ESP_LOGE(TAG_UI, "Config load failed");
        return;
    }
    s_cfg = &config::ha();
    s_ui_items_count = s_cfg->entity_count;
    if (s_ui_items_count <= 0)
        s_ui_items_count = 1;
    s_cur_item %= s_ui_items_count;

    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    /* Title label */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "KazDEV");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6E6E6), 0);
    lv_obj_set_style_text_font(title, &Montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    init_state_defaults();

    /* Initial selection shown */
    s_last_input_us = esp_timer_get_time();
    ui_show_current_item();

    /* Create perimeter ring (red by default) */
    if (s_status_ring == NULL)
    {
        int w = lv_obj_get_width(scr);
        int h = lv_obj_get_height(scr);
        int margin = 1;
        int r = (w < h ? w : h) / 2 - margin;
        if (r < 4)
            r = (w < h ? w : h) / 2 - 1;
        s_status_ring = create_perimeter_ring(scr, w / 2, h / 2, r, 0, 2, lv_color_hex(0xFF0000));
    }

    if (s_ha_ui_timer == NULL)
    {
        esp_timer_create_args_t args = {};
        args.callback = &ha_ui_timer_cb;
        args.name = "ui_ha_upd";
        esp_timer_create(&args, &s_ha_ui_timer);
        esp_timer_start_periodic(s_ha_ui_timer, 500 * 1000); /* 0.5s UI refresh */
    }

    // Subscribe to store for state/connection updates
    core::store_subscribe([](const core::AppState &st) {
        portENTER_CRITICAL(&s_state_mux);
        s_store_connected = st.connected;
        int count = st.entity_count;
        if (count > s_ui_items_count)
            count = s_ui_items_count;
        for (int i = 0; i < count; ++i)
        {
            strncpy(s_item_state[i], st.entities[i].value, sizeof(s_item_state[i]) - 1);
            s_item_state[i][sizeof(s_item_state[i]) - 1] = '\0';
        }
        s_store_dirty = true;
        portEXIT_CRITICAL(&s_state_mux);
    });
}

static void ha_ui_timer_cb(void *arg)
{
    (void)arg;
    bool online = false;
    bool needs_refresh = false;

    portENTER_CRITICAL(&s_state_mux);
    if (s_store_dirty)
    {
        s_store_dirty = false;
        needs_refresh = true;
    }
    online = s_store_connected;
    portEXIT_CRITICAL(&s_state_mux);

    if (!s_screensaver && online != s_last_online && s_status_ring)
    {
        lvgl_port_lock(-1);
        lv_color_t col = online ? lv_color_hex(0x00FF00) : lv_color_hex(0xFF0000);
        lv_obj_set_style_border_color(s_status_ring, col, 0);
        lvgl_port_unlock();
        s_last_online = online;
        needs_refresh = true;
    }

    if (needs_refresh && !s_screensaver)
    {
        ui_show_current_item();
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
    if (s_ui_items_count <= 0)
        return;
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
        ui_show_current_item();
        break;
    case 1:
        s_cur_item = (s_cur_item - 1 + s_ui_items_count) % s_ui_items_count;
        ui_show_current_item();
        break;
    default:
        break;
    }
    const config::Entity *ent = get_entity(s_cur_item);
    const char *ename = ent ? ent->name : "N/A";
    ESP_LOGI(TAG_UI, "%s | sel=%d:%s", knob_event_table[ev], s_cur_item, ename);
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
    if (!wifi_manager_is_connected())
    {
        setInfo("No WiFi");
        return;
    }
    const config::Entity *ent = get_entity(s_cur_item);
    if (!ent)
    {
        setInfo("No entity");
        return;
    }
    esp_err_t err = router::toggle(ent->entity_id);
    if (err == ESP_OK)
    {
        s_last_toggle_us = esp_timer_get_time();
        ESP_LOGI(TAG_UI, "Toggle sent for %s", ent->entity_id);
    }
    else
    {
        ESP_LOGW(TAG_UI, "Toggle failed for %s: %s", ent->entity_id, esp_err_to_name(err));
        setInfo("MQTT ERR");
    }
}

static void ui_show_current_item(void)
{
    char value[core::kEntityValueLen];
    copy_entity_value(s_cur_item, value, sizeof(value));
    if (value[0] == '\0')
    {
        value[0] = '-';
        value[1] = '\0';
    }
    const config::Entity *ent = get_entity(s_cur_item);
    const char *label = (ent && ent->name[0]) ? ent->name : "Entity";
    setLabel(label);
    setInfo(value);
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
    bool online_snapshot = false;
    portENTER_CRITICAL(&s_state_mux);
    online_snapshot = s_store_connected;
    portEXIT_CRITICAL(&s_state_mux);
    s_last_online = online_snapshot;
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
