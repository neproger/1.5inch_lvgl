#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>

// Board input pin assignment (shared between devices_init and UI).
// User button (BOOT)
#define BSP_BTN_PRESS GPIO_NUM_0
// Encoder phases
#define BSP_ENCODER_A GPIO_NUM_6
#define BSP_ENCODER_B GPIO_NUM_5
// Optional wakeup GPIO for light/deep sleep (use BOOT button GPIO0)
#define BSP_WAKE_GPIO GPIO_NUM_0

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize display, touch, LVGL port, encoder and button */
esp_err_t devices_init(void);

// Enable or disable LCD panel output (DCS DISPLAY_ON/OFF).
esp_err_t devices_display_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

