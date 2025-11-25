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

static void enter_config_mode()
{
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
    if (wifi_manager_init() != ESP_OK)
    {
        ESP_LOGE(TAG_APP, "WiFi manager init failed");
        return;
    }

    // Initialize config store (NVS backed).
    (void)config_store::init();

    if (!config_store::has_basic_config())
    {
        // No Wi‑Fi/HA config stored: enter setup mode.
        enter_config_mode();
    }

    if (devices_init() != ESP_OK)
    {
        return;
    }

    // Initialize application-level input mapping
    (void)input_controller::init();
    // Initialize toggle controller (handles TOGGLE_REQUEST/RESULT)
    (void)toggle_controller::init();

    // Log all events from the default ESP event loop for inspection/debugging
    (void)event_logger::init();

    /* Show splash as early as possible so user sees progress during bootstrap */
    lvgl_port_lock(0);
    ui::splash::show();
    ui::splash::update_progress(25); // WiFi + display/devices ready
    lvgl_port_unlock();

    if (!http_manager::bootstrap_state())
    {
        ESP_LOGE(TAG_APP, "Bootstrap over HTTPS failed, entering config mode");
        enter_config_mode();
    }

    lvgl_port_lock(0);
    ui::splash::update_progress(60); // State bootstrap finished
    lvgl_port_unlock();

    wifi_manager_start_auto(-85, 15000); // Keep Wi-Fi connected in background after bootstrap
    (void)router::start();               // Start connectivity via Router (currently MQTT)

    lvgl_port_lock(0);
    ui::splash::update_progress(80); // Connectivity ready
    lvgl_port_unlock();

    /* Create your UI under LVGL mutex */
    lvgl_port_lock(0);
    ui::screensaver::init_support();
    ui_app_init();
    ui::splash::update_progress(100); // UI fully initialized
    ui::splash::destroy();
    lvgl_port_unlock();

    ui::screensaver::start_weather_polling();
}
