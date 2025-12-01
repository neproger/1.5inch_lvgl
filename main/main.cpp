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
#include "esp_timer.h"
#include <stdbool.h>
#include <cstring>
#include "wifi_manager.h"
#include "http_manager.hpp"
#include "app/router.hpp"
#include "app/event_logger.hpp"
#include "app/input_controller.hpp"
#include "app/toggle_controller.hpp"
#include "app/app_state.hpp"
#include "app/app_events.hpp"

#include "config_server/config_store.hpp"
#include "config_server/config_server.hpp"

static const char *TAG_APP = "app";

static AppState g_app_state = AppState::BootDevices;

static void set_app_state(AppState new_state)
{
    if (g_app_state == new_state)
    {
        return;
    }
    AppState old = g_app_state;
    g_app_state = new_state;
    (void)app_events::post_app_state_changed(old, new_state, esp_timer_get_time(), false);
}

// Simple application-level idle controller for screensaver.
// For now it only decides when to show the screensaver; backlight
// dimming/sleep can be added later under the same pattern.
static void idle_controller_task(void *arg)
{
    (void)arg;

    static constexpr uint32_t kScreensaverTimeoutMs = 10000;

    for (;;)
    {
        lvgl_port_lock(0);
        lv_display_t *disp = lv_display_get_default();
        uint32_t inactive_ms = disp ? lv_display_get_inactive_time(disp) : 0;
        lvgl_port_unlock();

        if (g_app_state == AppState::NormalAwake &&
            inactive_ms >= kScreensaverTimeoutMs &&
            !ui::screensaver::is_active())
        {
            set_app_state(AppState::NormalScreensaver);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

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
    set_app_state(AppState::BootDevices);
    // Initialize devices (display, touch, LVGL) first so we can show splash early
    if (devices_init() != ESP_OK)
    {
        return;
    }

    ui::splash::show();
    ui::splash::update_state(10, "WiFi..."); // Display/devices ready

    set_app_state(AppState::BootWifi);
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

    // Initialize application-level input mapping
    (void)input_controller::init();
    // Initialize toggle controller (handles TOGGLE_REQUEST/RESULT)
    (void)toggle_controller::init();

    // Log all events from the default ESP event loop for inspection/debugging
    (void)event_logger::init();

    // React to REQUEST_CONFIG_MODE by entering config mode via FSM.
    {
        esp_event_handler_instance_t inst = nullptr;
        (void)esp_event_handler_instance_register(
            APP_EVENTS,
            app_events::REQUEST_CONFIG_MODE,
            [](void * /*arg*/, esp_event_base_t base, int32_t id, void * /*event_data*/)
            {
                if (base != APP_EVENTS || id != app_events::REQUEST_CONFIG_MODE)
                {
                    return;
                }
                set_app_state(AppState::ConfigMode);
                enter_config_mode();
            },
            nullptr,
            &inst);
    }

    // React to REQUEST_WAKE by transitioning back to NormalAwake.
    {
        esp_event_handler_instance_t inst = nullptr;
        (void)esp_event_handler_instance_register(
            APP_EVENTS,
            app_events::REQUEST_WAKE,
            [](void * /*arg*/, esp_event_base_t base, int32_t id, void * /*event_data*/)
            {
                if (base != APP_EVENTS || id != app_events::REQUEST_WAKE)
                {
                    return;
                }
                if (g_app_state == AppState::NormalScreensaver)
                {
                    set_app_state(AppState::NormalAwake);
                }
            },
            nullptr,
            &inst);
    }

    /* Show splash as early as possible so user sees progress during bootstrap */

    set_app_state(AppState::BootBootstrap);

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

        // Application is now in normal awake mode (rooms UI visible, MQTT running)
        set_app_state(AppState::NormalAwake);

        // Start idle controller task to drive screensaver based on LVGL inactivity.
        (void)xTaskCreate(idle_controller_task, "idle_ctrl", 4096, nullptr, 2, nullptr);

        ui::splash::destroy();

        http_manager::start_weather_polling();
    }
}
