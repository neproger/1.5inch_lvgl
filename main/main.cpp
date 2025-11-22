#include "devices_init.h"
#include "ui_app.h"
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

static const char *TAG_APP = "app";

extern "C" void app_main(void)
{
    if (wifi_manager_init() != ESP_OK)
    {
        ESP_LOGE(TAG_APP, "WiFi manager init failed");
        return;
    }

    if (devices_init() != ESP_OK)
    {
        return;
    }

    // Initialize application-level input mapping
    (void)input_controller::init();

    // Log all events from the default ESP event loop for inspection/debugging
    (void)event_logger::init();

    /* Show splash as early as possible so user sees progress during bootstrap */
    lvgl_port_lock(0);
    ui::splash::show();
    ui::splash::update_progress(25); // WiFi + display/devices ready
    lvgl_port_unlock();

    if (!http_manager::bootstrap_state())
    {
        ESP_LOGE(TAG_APP, "Bootstrap over HTTPS failed, stopping init");
        return;
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
