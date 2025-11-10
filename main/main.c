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
#include "wifi_manager.h"

void app_main(void)
{
    if (devices_init() != ESP_OK) {
        return;
    }

    // Wi‑Fi: инициализация и запуск авто‑поиска/подключения
    if (wifi_manager_init() == ESP_OK) {
        wifi_manager_start_auto(-85, 15000); // каждые 15с пробуем, если не подключены
    } else {
        ESP_LOGW("app", "WiFi init failed");
    }

    /* Create your UI under LVGL mutex */
    lvgl_port_lock(0);
    ui_app_init();
    lvgl_port_unlock();
}
