#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "iot_knob.h"
#include "iot_button.h"

#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_cst816s.h"

#include "devices_init.h"
#include "ui_app.h"

static const char *TAG = "devices";

/* LCD IO and panel */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
static esp_lcd_panel_handle_t lcd_panel = NULL;
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
//////////////////// Hardware configuration ///////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define EXAMPLE_LCD_HOST                  (SPI2_HOST)
#define EXAMPLE_LCD_BITS_PER_PIXEL        (16)
#define EXAMPLE_LCD_DRAW_BUFF_DOUBLE      (0)
#define EXAMPLE_LCD_LVGL_AVOID_TEAR       (1)
#define EXAMPLE_LCD_DRAW_BUFF_HEIGHT      (160)
/* Practical SPI/QSPI settings to avoid visual artifacts/tearing */
#define EXAMPLE_LCD_SPI_SPEED_MHZ         (40)   /* Was 40 MHz; 26 MHz is safer */
#define EXAMPLE_LCD_TRANS_QUEUE_DEPTH     (1)    /* Limit in-flight DMA transactions */
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL     1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL    !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_CS            (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_PCLK          (GPIO_NUM_10)
#define EXAMPLE_PIN_NUM_LCD_DATA0         (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA1         (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA2         (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_DATA3         (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_RST           (GPIO_NUM_8)
#define EXAMPLE_PIN_NUM_BK_LIGHT          (GPIO_NUM_17)

#define EXAMPLE_LCD_H_RES                 478
#define EXAMPLE_LCD_V_RES                 466

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL       (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL       (16)
#endif

/* Touch */
#define EXAMPLE_TOUCH_HOST                 (I2C_NUM_0)
#define EXAMPLE_TOUCH_I2C_NUM              (0)
#define EXAMPLE_TOUCH_I2C_CLK_HZ           (100000) /* Lower speed for stability */
#define EXAMPLE_PIN_NUM_TOUCH_SCL          (GPIO_NUM_3)
#define EXAMPLE_PIN_NUM_TOUCH_SDA          (GPIO_NUM_1)
#define EXAMPLE_PIN_NUM_TOUCH_RST          (GPIO_NUM_2)
#define EXAMPLE_PIN_NUM_TOUCH_INT          (GPIO_NUM_4)

/* Inputs */
typedef enum {
    BSP_BTN_PRESS = GPIO_NUM_0,
} bsp_button_t;

#define BSP_ENCODER_A         (GPIO_NUM_6)
#define BSP_ENCODER_B         (GPIO_NUM_5)

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Panel vendor init commands (SH8601) //////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t []){0x00}, 0, 0},
    {0xC4, (uint8_t []){0x80}, 1, 0},
    {0x3A, (uint8_t []){0x55}, 1, 0},
    {0x35, (uint8_t []){0x00}, 0, 10},
    {0x53, (uint8_t []){0x20}, 1, 10},
    {0x51, (uint8_t []){0xFF}, 1, 10},
    {0x63, (uint8_t []){0xFF}, 1, 10},
    /* Address window: revert to vendor defaults used previously
     * Columns: start=6, end=477  (0x0006 .. 0x01DD) -> width 472
     * Rows:    start=0, end=465  (0x0000 .. 0x01D1) -> height 466
     * Fine alignment is handled via esp_lcd_panel_set_gap below.
     */
    {0x2A, (uint8_t []){0x00,0x06,0x01,0xDD}, 4, 0},
    {0x2B, (uint8_t []){0x00,0x00,0x01,0xD1}, 4, 0},
    {0x11, (uint8_t []){0x00}, 0, 60},
    {0x29, (uint8_t []){0x00}, 0, 0},
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Touch init ///////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static esp_err_t app_touch_init(void)
{
    const i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = EXAMPLE_PIN_NUM_TOUCH_SDA,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,   /* Enable internal pull-ups unless external exist */
        .scl_io_num = EXAMPLE_PIN_NUM_TOUCH_SCL,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,   /* Enable internal pull-ups unless external exist */
        .master.clk_speed = EXAMPLE_TOUCH_I2C_CLK_HZ
    };
    ESP_RETURN_ON_ERROR(i2c_param_config(EXAMPLE_TOUCH_I2C_NUM, &i2c_conf), TAG, "I2C configuration failed");
    ESP_RETURN_ON_ERROR(i2c_driver_install(EXAMPLE_TOUCH_I2C_NUM, i2c_conf.mode, 0, 0, 0), TAG, "I2C initialization failed");

    /* Ensure INT pin has a pull-up (CST816S INT is usually active-low/open-drain) */
    gpio_config_t int_gpio_cfg = {
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = 1,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_gpio_cfg), TAG, "INT GPIO config failed");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_PIN_NUM_TOUCH_RST,
        .int_gpio_num = EXAMPLE_PIN_NUM_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)EXAMPLE_TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle), TAG, "new_panel_io_i2c failed");
    return esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, &touch_handle);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// LVGL init ////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static esp_err_t app_lvgl_init(void)
{
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,
        .task_stack = 7096,
        .task_affinity = -1,
        .task_max_sleep_ms = 500,
        .timer_period_ms = 5,
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = lcd_io,
        .panel_handle = lcd_panel,
        .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_DRAW_BUFF_HEIGHT,
        .double_buffer = EXAMPLE_LCD_DRAW_BUFF_DOUBLE,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .monochrome = false,
    #if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
    #endif
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
    #if LVGL_VERSION_MAJOR >= 9
            .swap_bytes = true,
    #endif
        }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    /* Register rounder callback under LVGL mutex to avoid race */
    lvgl_port_lock(-1);
    lv_display_add_event_cb(lvgl_disp, sh8601_lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lvgl_port_unlock();

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);

    return ESP_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Encoder and Button ///////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void knob_event_cb(void *arg, void *data)
{
    LVGL_knob_event(data);
}

static void knob_init(uint32_t encoder_a, uint32_t encoder_b)
{
    knob_config_t cfg = {
        .default_direction = 0,
        .gpio_encoder_a = encoder_a,
        .gpio_encoder_b = encoder_b,
#if CONFIG_PM_ENABLE
        .enable_power_save = true,
#endif
    };
    knob_handle_t knob = iot_knob_create(&cfg);
    assert(knob);
    esp_err_t err = iot_knob_register_cb(knob, KNOB_LEFT, knob_event_cb, (void *)KNOB_LEFT);
    err |= iot_knob_register_cb(knob, KNOB_RIGHT, knob_event_cb, (void *)KNOB_RIGHT);
    err |= iot_knob_register_cb(knob, KNOB_H_LIM, knob_event_cb, (void *)KNOB_H_LIM);
    err |= iot_knob_register_cb(knob, KNOB_L_LIM, knob_event_cb, (void *)KNOB_L_LIM);
    err |= iot_knob_register_cb(knob, KNOB_ZERO, knob_event_cb, (void *)KNOB_ZERO);
    ESP_ERROR_CHECK(err);
}

static void button_event_cb(void *arg, void *data)
{
    LVGL_button_event(data);
}

static void button_init(uint32_t button_num)
{
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = (gpio_num_t)button_num,
            .active_level = 0,
        },
    };
    button_handle_t btn = iot_button_create(&btn_cfg);
    assert(btn);
    esp_err_t err = iot_button_register_cb(btn, BUTTON_PRESS_DOWN, button_event_cb, (void *)BUTTON_PRESS_DOWN);
    err |= iot_button_register_cb(btn, BUTTON_PRESS_UP, button_event_cb, (void *)BUTTON_PRESS_UP);
    err |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT, button_event_cb, (void *)BUTTON_PRESS_REPEAT);
    err |= iot_button_register_cb(btn, BUTTON_PRESS_REPEAT_DONE, button_event_cb, (void *)BUTTON_PRESS_REPEAT_DONE);
    err |= iot_button_register_cb(btn, BUTTON_SINGLE_CLICK, button_event_cb, (void *)BUTTON_SINGLE_CLICK);
    err |= iot_button_register_cb(btn, BUTTON_DOUBLE_CLICK, button_event_cb, (void *)BUTTON_DOUBLE_CLICK);
    err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_START, button_event_cb, (void *)BUTTON_LONG_PRESS_START);
    err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_HOLD, button_event_cb, (void *)BUTTON_LONG_PRESS_HOLD);
    err |= iot_button_register_cb(btn, BUTTON_LONG_PRESS_UP, button_event_cb, (void *)BUTTON_LONG_PRESS_UP);
    err |= iot_button_register_cb(btn, BUTTON_PRESS_END, button_event_cb, (void *)BUTTON_PRESS_END);
    ESP_ERROR_CHECK(err);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Devices init /////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
esp_err_t devices_init(void)
{
    if (EXAMPLE_PIN_NUM_BK_LIGHT >= 0) {
        gpio_config_t bk_gpio_config = {
            .mode = GPIO_MODE_OUTPUT,
            .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
        };
        ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
        gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
}
 
    ESP_LOGI(TAG, "Initialize SPI/QSPI bus");
    const spi_bus_config_t buscfg =
        SH8601_PANEL_BUS_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_PCLK, EXAMPLE_PIN_NUM_LCD_DATA0,
                                     EXAMPLE_PIN_NUM_LCD_DATA1, EXAMPLE_PIN_NUM_LCD_DATA2,
                                     EXAMPLE_PIN_NUM_LCD_DATA3, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(EXAMPLE_LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = -1,
        .cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS,
        .pclk_hz = EXAMPLE_LCD_SPI_SPEED_MHZ * 1000 * 1000,
        .trans_queue_depth = EXAMPLE_LCD_TRANS_QUEUE_DEPTH,
        .lcd_cmd_bits = 32,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .flags = {
            .quad_mode = true, /* test stability without QSPI */
        },
    };

    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(sh8601_lcd_init_cmd_t),
        .flags = {
            .use_qspi_interface = 1,
        },
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)EXAMPLE_LCD_HOST, &io_config, &lcd_io));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_LOGI(TAG, "Install LCD driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(lcd_io, &panel_config, &lcd_panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(lcd_panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(lcd_panel, true));

    ESP_ERROR_CHECK(app_touch_init());
    /* Reduce noisy touch logs (optional): set CST816S tag to WARN */
    esp_log_level_set("CST816S", ESP_LOG_WARN);
    ESP_ERROR_CHECK(app_lvgl_init());

    knob_init(BSP_ENCODER_A, BSP_ENCODER_B);
    button_init(BSP_BTN_PRESS);

    return ESP_OK;
}

// --- Backlight control ---
esp_err_t devices_set_backlight_raw(uint8_t level)
{
    if (lcd_io == NULL) return ESP_ERR_INVALID_STATE;
    // SH8601 uses DCS 0x51 for brightness. Ensure BCTRL enabled via 0x53 (done in init).
    esp_err_t err;
    lvgl_port_lock(-1);
    err = esp_lcd_panel_io_tx_param(lcd_io, 0x51, &level, 1);
    lvgl_port_unlock();
    return err;
}

esp_err_t devices_set_backlight_percent(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint8_t level = (uint8_t)((percent * 255 + 50) / 100); // round
    return devices_set_backlight_raw(level);
}
