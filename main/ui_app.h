#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Initialize application UI (LVGL v9) */
    void ui_app_init(void);

    /* Initialize screensaver (idle timer, screens) */
    void ui_init_screensaver_support(void);

    /* Start periodic weather HTTP polling */
    void ui_start_weather_polling(void);

    /* Optional: splash screen helpers for early boot */
    void ui_show_boot_splash(void);
    void ui_update_boot_splash(int percent);
    void ui_hide_boot_splash(void);

    /* Optional: input event hooks used by device init */
    void LVGL_knob_event(void *event);
    void LVGL_button_event(void *event);

#ifdef __cplusplus
}
#endif
