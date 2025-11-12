#include "ui_app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "fonts.h"
#include "ha_mqtt.hpp"
#include "ha_mqtt_config.h"
#include "devices_init.h"
#include "wifi_manager.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include "cJSON.h"

static const char *TAG_UI = "UI";
static lv_obj_t *s_status_label = NULL;
static lv_obj_t *s_status_ring = NULL;
static lv_obj_t *s_info_label = NULL; /* Secondary info/description label */
static TaskHandle_t s_ha_req_task = NULL;
static esp_timer_handle_t s_ha_ui_timer = NULL; /* periodic UI updater */
static bool s_last_online = false;
static int64_t s_last_toggle_us = 0; /* simple cooldown to avoid hammering */

/* Multi-state UI: selectable HA entities + screensaver */
typedef struct
{
    const char *name;      /* shown */
    const char *entity_id; /* HA entity */
} ui_item_t;

static const ui_item_t s_ui_items[] = {
    {"Кухня", "switch.wifi_breaker_t_switch_1"},
    {"Коридор", "switch.wifi_breaker_t_switch_2"},
};
static const int s_ui_items_count = sizeof(s_ui_items) / sizeof(s_ui_items[0]);
static int s_cur_item = 0;

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
void setInfo(const char *text);
void setLabel(const char *text);

static char s_item_state[s_ui_items_count][12] = {"-", "-"};
static void ui_mqtt_on_msg(const char *topic, const char *data, int len)
{
    const char prefix[] = "ha/state/";
    const size_t pfx_len = sizeof(prefix) - 1;
    if (!topic)
        return;
    ESP_LOGI(TAG_UI, "MQTT msg received: topic='%s', len=%d, data='%.*s'", topic, len, len, data ? data : "");
    if (strncmp(topic, prefix, pfx_len) != 0)
        return;
    const char *ent = topic + pfx_len;
    for (int i = 0; i < s_ui_items_count; ++i)
    {
        if (strcmp(ent, s_ui_items[i].entity_id) == 0)
        {
            int n = (len < (int)sizeof(s_item_state[i]) - 1) ? len : (int)sizeof(s_item_state[i]) - 1;
            if (n < 0)
                n = 0;
            memcpy(s_item_state[i], data ? data : "", n);
            s_item_state[i][n] = '\0';
            if (i == s_cur_item)
            {
                setInfo(s_item_state[i]);
            }
            break;
        }
    }
}

extern "C" void ui_app_init(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    /* Title label */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "KazDEV");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6E6E6), 0);
    lv_obj_set_style_text_font(title, &Montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

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

    // Subscribe to state topics for each configured entity
    ha_mqtt::set_message_handler(&ui_mqtt_on_msg);
    for (int i = 0; i < s_ui_items_count; ++i)
    {
        char topic[128];
        snprintf(topic, sizeof(topic), "ha/state/%s", s_ui_items[i].entity_id);
        ha_mqtt::subscribe(topic, 1);
    }
}

static void ha_ui_timer_cb(void *arg)
{
    (void)arg;
    bool online = false;
    online = ha_mqtt::is_connected();
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
        ui_show_current_item();
        break;
    case 1:
        s_cur_item = (s_cur_item - 1 + s_ui_items_count) % s_ui_items_count;
        ui_show_current_item();
        break;
    default:
        break;
    }
    ESP_LOGI(TAG_UI, "%s | sel=%d:%s", knob_event_table[ev], s_cur_item, s_ui_items[s_cur_item].name);
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
    if (!ha_mqtt::is_connected())
    {
        setLabel("No MQTT");
        return;
    }
    if (s_ha_req_task == NULL)
    {
        BaseType_t ok = xTaskCreate(ha_toggle_task, "ha_toggle", 6144, NULL, 4, &s_ha_req_task);
        if (ok != pdPASS)
        {
            ESP_LOGW(TAG_UI, "Failed to create ha_toggle task");
        }
    }
}

static void ha_toggle_task(void *arg)
{
    (void)arg;
    setInfo("-");
    if (!wifi_manager_is_connected())
    {
        setInfo("No WiFi");
        s_ha_req_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    const char *entity = s_ui_items[s_cur_item].entity_id;
    esp_err_t err = ha_mqtt::publish_toggle(entity);
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
    setLabel(s_ui_items[s_cur_item].name);
    setInfo(s_item_state[s_cur_item]);
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
