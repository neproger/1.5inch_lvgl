#include "devices_init.h"
#include "ui_app.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"

void app_main(void)
{
    if (devices_init() != ESP_OK) {
        return;
    }

    /* Create your UI under LVGL mutex */
    lvgl_port_lock(0);
    ui_app_init();
    lvgl_port_unlock();
}
