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

    // Wi‑Fi: инициализация и попытка подключиться к лучшей доступной сети
    if (wifi_manager_init() == ESP_OK) {
        if (wifi_manager_connect_best_known(-85) == ESP_OK) {
            bool got_ip = wifi_manager_wait_ip(10000);
            if (got_ip) {
                ESP_LOGI("app", "WiFi connected and IP obtained");
            } else {
                ESP_LOGI("app", "WiFi connected? waiting IP timeout");
            }
        } else {
            ESP_LOGW("app", "No known WiFi in range");
        }
    } else {
        ESP_LOGW("app", "WiFi init failed");
    }

    /* Create your UI under LVGL mutex */
    lvgl_port_lock(0);
    ui_app_init();
    lvgl_port_unlock();
}
