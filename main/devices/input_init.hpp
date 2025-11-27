/**
 * Simple input devices module: rotary encoder + button.
 *
 * Provides a single initialization entry used by devices_init().
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize rotary encoder and front button and bind them to LVGL.
 *
 * Uses board-specific GPIO definitions (BSP_ENCODER_A/B, BSP_BTN_PRESS)
 * configured elsewhere.
 */
esp_err_t devices_input_init(void);

#ifdef __cplusplus
}
#endif
