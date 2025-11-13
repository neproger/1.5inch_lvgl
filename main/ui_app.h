#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* Initialize application UI (LVGL v9) */
    void ui_app_init(void);

    /* Optional: input event hooks used by device init */
    void LVGL_knob_event(void *event);
    void LVGL_button_event(void *event);

    /* Convenience: set central status label text */
    void setLabel(const char *text);

#ifdef __cplusplus
}
#endif
