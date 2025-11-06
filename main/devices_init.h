#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize display, touch, LVGL port, encoder and button */
esp_err_t devices_init(void);

#ifdef __cplusplus
}
#endif

