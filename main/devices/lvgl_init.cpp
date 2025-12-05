#include "lvgl_init.hpp"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "display_init.hpp"

static const char *TAG_LVGL = "devices_lvgl";

static lv_display_t *s_lvgl_disp = nullptr;
static lv_indev_t *s_lvgl_touch_indev = nullptr;

// SH8601 requires x/y coordinates aligned to 2-pixel boundaries.
// Round LVGL invalidated areas accordingly to avoid visual artifacts.
static void sh8601_lvgl_rounder_cb(lv_event_t *e)
{
    lv_area_t *area = (lv_area_t *)lv_event_get_param(e);
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;
    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

esp_err_t devices_lvgl_init(esp_lcd_touch_handle_t touch_handle)
{
    if (s_lvgl_disp)
    {
        return ESP_OK;
    }

    lvgl_port_cfg_t lvgl_cfg = {};
    lvgl_cfg.task_priority = 4;
    lvgl_cfg.task_stack = 6144;
    lvgl_cfg.task_affinity = -1;
    lvgl_cfg.task_max_sleep_ms = 100;
    lvgl_cfg.timer_period_ms = 4;
    esp_err_t err = lvgl_port_init(&lvgl_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_LVGL, "LVGL port initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    lvgl_port_display_cfg_t disp_cfg = {};
    disp_cfg.io_handle = devices_display_get_panel_io();
    disp_cfg.panel_handle = devices_display_get_panel();
    disp_cfg.buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT;
    disp_cfg.double_buffer = LCD_DRAW_BUFF_DOUBLE;
    disp_cfg.hres = LCD_H_RES;
    disp_cfg.vres = LCD_V_RES;
    disp_cfg.monochrome = false;
    disp_cfg.color_format = LV_COLOR_FORMAT_RGB565;
    disp_cfg.rotation.swap_xy = false;
    disp_cfg.rotation.mirror_x = false;
    disp_cfg.rotation.mirror_y = false;
    disp_cfg.flags.buff_dma = true;
    disp_cfg.flags.swap_bytes = true;

    s_lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    lvgl_port_lock(-1);
    // Panel active area is shifted by 6 px in X (see 0x2A init in display_init.cpp).
    // Tell LVGL that logical (0,0) corresponds to physical (6,0).
    lv_display_set_offset(s_lvgl_disp, 6, 0);

    // твой новый цвет
    lv_color_t primary = lv_color_hex(0xd34836);
    lv_color_t secondary = lv_color_hex(0x303030);

    const lv_font_t *font = LV_FONT_DEFAULT;

    lv_theme_t *theme = lv_theme_default_init(s_lvgl_disp, primary, secondary, true, font);
    lv_display_set_theme(s_lvgl_disp, theme);

    lv_display_add_event_cb(s_lvgl_disp, sh8601_lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lvgl_port_unlock();

    if (touch_handle)
    {
        lvgl_port_touch_cfg_t touch_cfg = {};
        touch_cfg.disp = s_lvgl_disp;
        touch_cfg.handle = touch_handle;
        s_lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);
    }
    else
    {
        ESP_LOGW(TAG_LVGL, "LVGL touch not added: no touch handle");
    }

    return ESP_OK;
}

esp_err_t devices_lvgl_deinit(void)
{
    esp_err_t err = lvgl_port_deinit();
    s_lvgl_disp = nullptr;
    s_lvgl_touch_indev = nullptr;
    return err;
}
