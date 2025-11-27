#include "touch_init.hpp"

#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_lcd_touch.h"
#include "esp_lcd_touch_cst816s.h"

#include "display_init.hpp"

static const char *TAG_TOUCH = "devices_touch";

// Touch controller configuration (CST816S over I2C)
#define TOUCH_I2C_NUM I2C_NUM_0
#define TOUCH_I2C_CLK_HZ (100000)
#define PIN_TOUCH_SCL (GPIO_NUM_3)
#define PIN_TOUCH_SDA (GPIO_NUM_1)
#define PIN_TOUCH_RST (GPIO_NUM_2)
#define PIN_TOUCH_INT (GPIO_NUM_4)

esp_err_t devices_touch_init(esp_lcd_touch_handle_t *out_handle)
{
    if (!out_handle)
    {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = PIN_TOUCH_SDA;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_io_num = PIN_TOUCH_SCL;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = TOUCH_I2C_CLK_HZ;
    esp_err_t err = i2c_param_config(TOUCH_I2C_NUM, &i2c_conf);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TOUCH, "I2C configuration failed: %s", esp_err_to_name(err));
        return err;
    }
    err = i2c_driver_install(TOUCH_I2C_NUM, i2c_conf.mode, 0, 0, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TOUCH, "I2C initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    gpio_config_t int_gpio_cfg = {};
    int_gpio_cfg.pin_bit_mask = 1ULL << PIN_TOUCH_INT;
    int_gpio_cfg.mode = GPIO_MODE_INPUT;
    int_gpio_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    int_gpio_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    int_gpio_cfg.intr_type = GPIO_INTR_DISABLE;
    err = gpio_config(&int_gpio_cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TOUCH, "INT GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_lcd_touch_config_t tp_cfg = {};
    tp_cfg.x_max = LCD_H_RES;
    tp_cfg.y_max = LCD_V_RES;
    tp_cfg.rst_gpio_num = PIN_TOUCH_RST;
    tp_cfg.int_gpio_num = PIN_TOUCH_INT;
    tp_cfg.levels.reset = 0;
    tp_cfg.levels.interrupt = 0;
    tp_cfg.flags.swap_xy = 0;
    tp_cfg.flags.mirror_x = 0;
    tp_cfg.flags.mirror_y = 0;

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;

#ifdef ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG
    const esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_CST816S_CONFIG();
    err = esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)TOUCH_I2C_NUM, &tp_io_config, &tp_io_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TOUCH, "new_panel_io_i2c failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_lcd_touch_new_i2c_cst816s(tp_io_handle, &tp_cfg, out_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG_TOUCH, "touch_new_i2c_cst816s failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
#else
    ESP_LOGW(TAG_TOUCH, "CST816S touch driver not available, skipping touch init");
    (void)tp_io_handle;
    *out_handle = NULL;
    return ESP_OK;
#endif
}
