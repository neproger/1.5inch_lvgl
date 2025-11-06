#include "ui_app.h"

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
    lv_obj_t *label = lv_label_create(scr);
    lv_label_set_text(label, "Hello, LVGL v9!");
    lv_obj_center(label);
}

void LVGL_knob_event(void *event)
{
    LV_UNUSED(event);
}

void LVGL_button_event(void *event)
{
    LV_UNUSED(event);
}
