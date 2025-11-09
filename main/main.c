#include "devices_init.h"
#include "ui_app.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include <stdbool.h>


// GPIO used to power/control external device
#define GPIO_40   GPIO_NUM_40



void app_main(void)
{
    if (devices_init() != ESP_OK) {
        return;
    }

    /* Create your UI under LVGL mutex */
    lvgl_port_lock(0);
    ui_app_init();
    lvgl_port_unlock();

    // Initialize/power external peripherals on GPIO40
    extern void start_pin_40(void);
    start_pin_40();




}

void start_pin_40(void)
{
    static bool started = false;
    if (started) return;
    started = true;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << GPIO_40,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_drive_capability(GPIO_40, GPIO_DRIVE_CAP_0);
    int lvl = 1;
    gpio_set_level(GPIO_40, lvl);
}
