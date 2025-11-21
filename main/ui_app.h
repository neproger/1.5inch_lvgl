#pragma once

#include "lvgl.h"

#ifdef __cplusplus
/* C++ UI API */

/* Initialize application UI (LVGL v9) */
void ui_app_init(void);

/* Initialize screensaver (idle timer, screens) */
void ui_init_screensaver_support(void);

extern "C"
{
#endif

    /* Optional: input event hooks used by device init (C API) */
    void LVGL_knob_event(void *event);
    void LVGL_button_event(void *event);

#ifdef __cplusplus
}
#endif
