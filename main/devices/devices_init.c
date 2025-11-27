#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"

#include "devices_init.h"
#include "ui/ui_app.hpp"
#include "devices/display_init.hpp"
#include "devices/touch_init.hpp"
#include "devices/input_init.hpp"

// Optional debug task can conflict with LVGL touch polling; keep disabled by default
#define ENABLE_TOUCH_DEBUG 0

static const char *TAG = "devices";

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

/* SH8601 requires x/y coordinates aligned to 2-pixel boundaries.
 * Round LVGL invalidated areas accordingly to avoid visual artifacts. */
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Touch init wrapper ////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static esp_err_t app_touch_init(void)
{
    return devices_touch_init(&touch_handle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// LVGL init ////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static esp_err_t app_lvgl_init(void)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 6144,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 8,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = devices_display_get_panel_io(),
        .panel_handle = devices_display_get_panel(),
        .buffer_size = LCD_H_RES * LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = LCD_DRAW_BUFF_DOUBLE,
        .hres = LCD_H_RES,
        .vres = LCD_V_RES,
        .monochrome = false,
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = false,
                .mirror_y = false,
            },
        .flags =
            {
                .buff_dma = true,
#if LVGL_VERSION_MAJOR >= 9
                .swap_bytes = true,
#endif
            }};
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    /* Register rounder callback under LVGL mutex to avoid race */
    lvgl_port_lock(-1);
    lv_display_add_event_cb(lvgl_disp, sh8601_lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lvgl_port_unlock();

    if (touch_handle)
    {
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = lvgl_disp,
            .handle = touch_handle,
        };
        lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);
    }
    else
    {
        ESP_LOGW(TAG, "LVGL touch not added: no touch handle");
    }

    return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Devices init /////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
esp_err_t devices_init(void)
{
    esp_err_t err = ESP_OK;

    err = devices_display_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = app_touch_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(err));
        return err;
    }
    esp_log_level_set("CST816S", ESP_LOG_NONE);

    err = app_lvgl_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "LVGL init failed: %s", esp_err_to_name(err));
        return err;
    }

    devices_input_init();

    return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Panel on/off control (public API) /////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

esp_err_t devices_display_set_enabled(bool enabled)
{
    esp_lcd_panel_handle_t panel = devices_display_get_panel();
    if (!panel)
    {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_lcd_panel_disp_on_off(panel, enabled);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "LCD panel disp_on_off(%d) failed: %s", enabled ? 1 : 0, esp_err_to_name(err));
    }
    return err;
}

