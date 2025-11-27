#include "input_init.hpp"

#include <assert.h>

#include "esp_err.h"
#include "esp_log.h"

#include "iot_knob.h"
#include "iot_button.h"

#include "esp_lvgl_port.h"
#include "ui/ui_app.hpp"
#include "devices_init.h"

static const char *TAG_INPUT = "devices_input";

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////// Encoder and Button ///////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void knob_event_cb(void *arg, void *data)
{
    (void)arg;
    /* iot_knob callbacks run in esp_timer task context.
     * Protect LVGL with lvgl_port_lock to avoid races/WDT. */
    lvgl_port_lock(0);
    LVGL_knob_event(data);
    lvgl_port_unlock();
}

static void knob_init(uint32_t encoder_a, uint32_t encoder_b)
{
    knob_config_t cfg = {};
    cfg.default_direction = 0;
    cfg.gpio_encoder_a = static_cast<uint8_t>(encoder_a);
    cfg.gpio_encoder_b = static_cast<uint8_t>(encoder_b);
#if CONFIG_PM_ENABLE
    cfg.enable_power_save = true;
#endif
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
    (void)arg;
    /* Button callbacks also run outside the LVGL task; protect LVGL. */
    lvgl_port_lock(0);
    LVGL_button_event(data);
    lvgl_port_unlock();
}

static void button_init(uint32_t button_num)
{
    button_config_t btn_cfg = {};
    btn_cfg.type = BUTTON_TYPE_GPIO;
    btn_cfg.gpio_button_config.gpio_num = static_cast<gpio_num_t>(button_num);
    btn_cfg.gpio_button_config.active_level = 0;
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

esp_err_t devices_input_init(void)
{
    ESP_LOGD(TAG_INPUT, "Initializing input devices (encoder + button)");

    knob_init(BSP_ENCODER_A, BSP_ENCODER_B);
    button_init(BSP_BTN_PRESS);

    return ESP_OK;
}
