#include "ui_app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

static const char *TAG_UI = "UI";
static lv_obj_t *s_status_label = NULL;
/* Simple UI-side debounce/throttle for knob updates */
static int s_knob_value = 0;           /* Accumulated logical knob value */
static int s_knob_shown_value = 0;     /* Last value shown on the label */
static int64_t s_last_update_us = 0;   /* Last UI update timestamp (us) */
static const int64_t UI_UPDATE_INTERVAL_US = 100 * 1000; /* 100 ms */
/* When true, suppress knob text updates while a button message is shown */
static volatile bool s_showing_button = false;
/* One-shot timer to revert button text back to the counter */
static esp_timer_handle_t s_button_revert_timer = NULL;

/* Forward declarations for helpers used below */
static lv_obj_t* create_dot(lv_obj_t *parent, int x, int y, int d, lv_color_t color);
static void create_ring_of_dots(lv_obj_t *parent, int cx, int cy, int radius, int n, int d, lv_color_t color);
static void handle_single_click(void);

void ui_app_init(void)
{
    lv_obj_t *scr = lv_screen_active();

    /* Optional: set a neutral background */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);

    /* Title label */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "KazDEV");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6E6E6), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    /* Center message */
    setLabel("READY");

    // пример использования (центр экрана 236x233 при 472x466):
    create_ring_of_dots(scr, 472/2, 466/2, 218, 12, 8, lv_color_hex(0xE6E6E6));
}

void setLabel(const char *text)
{
    if (text == NULL) {
        text = "";
    }
    lvgl_port_lock(-1);
    if (s_status_label == NULL) {
        lv_obj_t *scr = lv_screen_active();
        s_status_label = lv_label_create(scr);
        lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_48, 0);
        lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xE6E6E6), 0);
        lv_obj_center(s_status_label);
    }
    lv_label_set_text(s_status_label, text);
    lvgl_port_unlock();
}

static const char *knob_event_table[] = {
    "KNOB_RIGHT",
    "KNOB_LEFT",
    "KNOB_H_LIM",
    "KNOB_L_LIM",
    "KNOB_ZERO",
};
void LVGL_knob_event(void *event)
{
    /* Convert event code from callback */
    int ev = (int)(intptr_t)event;

    /* Update logical value based on event kind */
    switch (ev) {
    case 0: /* KNOB_RIGHT */
        s_knob_value++;
        break;
    case 1: /* KNOB_LEFT */
        s_knob_value--;
        break;
    case 4: /* KNOB_ZERO */
        s_knob_value = 0;
        break;
    default:
        break;
    }

    /* Throttle UI updates to avoid flicker/smearing */
    int64_t now = esp_timer_get_time();
    if (!s_showing_button && (now - s_last_update_us) >= UI_UPDATE_INTERVAL_US && s_knob_shown_value != s_knob_value) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", s_knob_value);
        setLabel(buf);
        s_knob_shown_value = s_knob_value;
        s_last_update_us = now;
    }
    ESP_LOGI(TAG_UI, "%s | %d", knob_event_table[ev], s_knob_value);
}

static const int64_t UI_BUTTON_SHOW_INTERVAL_US = 2000 * 1000; /* 2 s */
static void button_revert_cb(void *arg)
{
    /* Timer callback: switch label back to the counter */
    s_showing_button = false;
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", s_knob_value);
    setLabel(buf);
    s_knob_shown_value = s_knob_value;
    s_last_update_us = esp_timer_get_time();
}

static const char *button_event_table[] = {
    "PRESS_DOWN",           // 0
    "PRESS_UP",             // 1
    "PRESS_REPEAT",         // 2
    "PRESS_REPEAT_DONE",    // 3
    "СБРОС",                // 4
    "DOUBLE_CLICK",         // 5
    "MULTIPLE_CLICK",       // 6
    "LONG_PRESS_START",     // 7
    "LONG_PRESS_HOLD",      // 8
    "LONG_PRESS_UP",        // 9
    "PRESS_END",            // 10
};
void LVGL_button_event(void *event)
{
    int ev = (int)(intptr_t)event;

    const int table_sz = sizeof(button_event_table) / sizeof(button_event_table[0]);
    const char *label = (ev >= 0 && ev < table_sz) ? button_event_table[ev] : "BUTTON_UNKNOWN";

    switch (ev) {
        case 4: // SINGLE_CLICK
            ESP_LOGI(TAG_UI, "%s", label);
            // Обновление LVGL метки (чтобы показывалось, что нажато)
            setLabel(label);
            handle_single_click();
            break;

        case 5: // DOUBLE_CLICK
            ESP_LOGI(TAG_UI, "%s", label);
            setLabel(label);
            // handle_double_click();
            break;

        case 7: // LONG_PRESS_START
            ESP_LOGI(TAG_UI, "%s", label);
            setLabel(label);
            // handle_long_press_start();
            break;

        default:
            break;
    }
    
    s_showing_button = true;
    if (s_button_revert_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = &button_revert_cb,
            .arg = NULL,
            .name = "ui_btn_revert",
        };
        esp_timer_create(&args, &s_button_revert_timer);
    }
    if (s_button_revert_timer) {
        esp_timer_stop(s_button_revert_timer);
        esp_timer_start_once(s_button_revert_timer, UI_BUTTON_SHOW_INTERVAL_US);
    }
}


static void handle_single_click(void)
{
    s_knob_value = 0;
}

static lv_obj_t* create_dot(lv_obj_t *parent, int x, int y, int d, lv_color_t color)
{
    lv_obj_t *dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, d, d);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    // позиционируем по центру точки
    lv_obj_set_pos(dot, x - d/2, y - d/2);
    return dot;
}

static void create_ring_of_dots(lv_obj_t *parent, int cx, int cy, int radius, int n, int d, lv_color_t color)
{
    for(int i = 0; i < n; i++) {
        float a = (2.0f * 3.1415926f * i) / n;
        int x = cx + (int)(radius * cosf(a));
        int y = cy + (int)(radius * sinf(a));
        create_dot(parent, x, y, d, color);
    }
}
