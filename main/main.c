#include "devices_init.h"
#include "ui_app.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp32-dht11.h"
#include <stdbool.h>

#define CONFIG_DHT11_PIN GPIO_NUM_39
#define CONFIG_CONNECTION_TIMEOUT 5

void app_main(void)
{
    if (devices_init() != ESP_OK) {
        return;
    }

    /* Create your UI under LVGL mutex */
    lvgl_port_lock(0);
    ui_app_init();
    lvgl_port_unlock();

    // Start SSR first (powers DHT), then DHT reader
    extern void start_ssr40_blink(void);
    start_ssr40_blink();

    dht11_t dht11_sensor;
    dht11_sensor.dht11_pin = CONFIG_DHT11_PIN;

    // Read data
    while(1)
    {
        if(!dht11_read(&dht11_sensor, CONFIG_CONNECTION_TIMEOUT))
        {  
            printf("[Temperature]> %.2f \n",dht11_sensor.temperature);
            printf("[Humidity]> %.2f \n",dht11_sensor.humidity);
        }
        vTaskDelay(2000/portTICK_PERIOD_MS);
    } 
}

#define APP_SSR_GPIO   GPIO_NUM_40
void start_ssr40_blink(void)
{
    static bool started = false;
    if (started) return;
    started = true;
    // xTaskCreate(ssr_blink_task, "ssr40", 2048, NULL, 2, NULL);
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << APP_SSR_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_drive_capability(APP_SSR_GPIO, GPIO_DRIVE_CAP_0);
    int lvl = 1;
    gpio_set_level(APP_SSR_GPIO, lvl);
}
