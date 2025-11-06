#include "ui_app.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

static const char *TAG_UI = "ui";
static lv_obj_t *s_status_label = NULL;

void ui_app_init(void)
{
    lv_obj_t *scr = lv_screen_active();

    /* Optional: set a neutral background */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), 0);

    /* Title label */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "My UI");
    lv_obj_set_style_text_color(title, lv_color_hex(0xE6E6E6), 0);
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    /* Center message */
    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Hello, LVGL v9!");
    lv_obj_center(s_status_label);
}

void LVGL_knob_event(void *event)
{
    /* Called from devices_init callbacks; lock if touching LVGL */
    lvgl_port_lock(-1);
    char buf[32];
    snprintf(buf, sizeof(buf), "Knob: %d", (int)event);
    lv_label_set_text(s_status_label, buf);
    lvgl_port_unlock();
    ESP_LOGI(TAG_UI, "Knob event: %d", (int)event);
}

void LVGL_button_event(void *event)
{
    lvgl_port_lock(-1);
    char buf[32];
    snprintf(buf, sizeof(buf), "Button: %d", (int)event);
    lv_label_set_text(s_status_label, buf);
    lvgl_port_unlock();
    ESP_LOGI(TAG_UI, "Button event: %d", (int)event);
}
