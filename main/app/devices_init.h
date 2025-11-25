#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

// Board input pin assignment (shared between devices_init and UI).
// User button (BOOT)
#define BSP_BTN_PRESS GPIO_NUM_0
// Encoder phases
#define BSP_ENCODER_A GPIO_NUM_6
#define BSP_ENCODER_B GPIO_NUM_5

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize display, touch, LVGL port, encoder and button */
esp_err_t devices_init(void);

// Set display backlight brightness
// percent: 0..100
esp_err_t devices_set_backlight_percent(int percent);

// Raw brightness level via DCS 0x51 (0..255)
esp_err_t devices_set_backlight_raw(uint8_t level);

#ifdef __cplusplus
}
#endif

