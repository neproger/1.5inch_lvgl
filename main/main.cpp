#include "devices_init.h"
#include "ui/ui_app.hpp"
#include "ui/splash.hpp"
#include "ui/screensaver.hpp"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include <stdbool.h>
#include <cstring>
#include "wifi_manager.h"
#include "http_manager.hpp"
#include "app/router.hpp"
#include "app/event_logger.hpp"
#include "app/input_controller.hpp"
#include "app/toggle_controller.hpp"

#include "config_server/config_store.hpp"
#include "config_server/config_server.hpp"

static const char *TAG_APP = "app";

/* static void gpio_debug_task(void *arg)
{
    (void)arg;

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << BSP_WAKE_GPIO) | (1ULL << GPIO_NUM_40);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    int last_39 = -1;
    int last_40 = -1;

    while (true)
    {
        int level_39 = gpio_get_level(GPIO_NUM_39);
        int level_40 = gpio_get_level(GPIO_NUM_40);

        if (level_39 != last_39)
        {
            ESP_LOGI("gpio_debug", "GPIO39=%d", level_39);
            last_39 = level_39;
        }

        if (level_40 != last_40)
        {
            ESP_LOGI("gpio_debug", "GPIO40=%d", level_40);
            last_40 = level_40;
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
} */

static void enter_config_mode()
{
    ui::splash::update_state(100, "Настройка...");
    // Start configuration access point + HTTP UI and park main task.
    (void)wifi_manager_start_ap_config("esp32-ha-setup", nullptr);
    (void)config_server::start();
    ESP_LOGI(TAG_APP, "Config mode: AP 'esp32-ha-setup' with HTTP config server");
    for (;;)
    {
        vTaskDelay(portMAX_DELAY);
    }
}

extern "C" void app_main(void)
{
    // Initialize devices (display, touch, LVGL) first so we can show splash early
    if (devices_init() != ESP_OK)
    {
        return;
    }

    ui::splash::show(&enter_config_mode);
    ui::splash::update_state(10, "WiFi..."); // Display/devices ready

    if (wifi_manager_init() != ESP_OK)
    {
        ESP_LOGE(TAG_APP, "WiFi manager init failed");
        return;
    }

    // Initialize config store (NVS backed).
    (void)config_store::init();

    if (!config_store::has_basic_config())
    {
        // No Wi-Fi/HA config stored: user can enter setup from splash button.
        ESP_LOGW(TAG_APP, "No Wi-Fi/HA config stored; use setup button on splash");
    }

    // Debug task to monitor GPIO39/40 levels
    // xTaskCreate(gpio_debug_task, "gpio_debug", 2048, nullptr, 1, nullptr);

    // Initialize application-level input mapping
    (void)input_controller::init();
    // Initialize toggle controller (handles TOGGLE_REQUEST/RESULT)
    (void)toggle_controller::init();

    // Log all events from the default ESP event loop for inspection/debugging
    (void)event_logger::init();

    /* Show splash as early as possible so user sees progress during bootstrap */

    ui::splash::update_state(50, "Подключение..."); // WiFi + display/devices ready

    if (!http_manager::bootstrap_state())
    {
        ESP_LOGE(TAG_APP, "Bootstrap over HTTPS failed, entering config mode");
        ESP_LOGW(TAG_APP, "No Wi-Fi/HA config stored; use setup button on splash");
    }
    else
    {
        ui::splash::update_state(100, "Готово");
        wifi_manager_start_auto(-85, 15000); // Keep Wi-Fi connected in background after bootstrap
        (void)router::start();               // Start connectivity via Router (currently MQTT)

        // Build screensaver first so ui_app_init can attach input callbacks to it
        ui::screensaver::init_support();
        ui_app_init();
        
        ui::splash::destroy();

        http_manager::start_weather_polling();
    }
}
