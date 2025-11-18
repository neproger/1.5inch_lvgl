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
#include <cstring>
#include "wifi_manager.h"
#include "http_utils.h"
#include "ha_http_config.h"
#include "state_manager.hpp"
#include "app/router.hpp"

static const char *TAG_APP = "app";
static constexpr const char *kBootstrapUrl = HA_HTTP_BOOTSTRAP_URL;
static constexpr const char *kBootstrapToken = HA_HTTP_BEARER_TOKEN;

// HA-style template request body used for bootstrap HTTP call.
static const char *kBootstrapTemplateBody = R"json(
{
  "template": "AREA_ID,AREA_NAME,ENTITY_ID,ENTITY_NAME,STATE\n{% for area in areas() -%}\n{% for e in area_entities(area) -%}\n{{ area }},{{ area_name(area) }},{{ e }},{{ states[e].name }},{{ states[e].state }}\n{% endfor %}\n{% endfor %}"
}
)json";

static bool ensure_wifi_connected(void)
{
    if (wifi_manager_is_connected())
    {
        return true;
    }

    esp_err_t err = wifi_manager_connect_best_known(-85);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG_APP, "WiFi connect failed: %s", esp_err_to_name(err));
        return false;
    }

    if (!wifi_manager_wait_ip(15000))
    {
        ESP_LOGW(TAG_APP, "WiFi connect timeout");
        return false;
    }
    return true;
}

static bool perform_bootstrap_request(void)
{
    char buf[2048];
    int status = 0;
    const char *token = (kBootstrapToken && kBootstrapToken[0]) ? kBootstrapToken : nullptr;
    esp_err_t err = http_send("POST", kBootstrapUrl, kBootstrapTemplateBody, "application/json", token, buf, sizeof(buf), &status);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG_APP, "Bootstrap HTTP error: %s", esp_err_to_name(err));
        return false;
    }
    if (status < 200 || status >= 300)
    {
        ESP_LOGW(TAG_APP, "Bootstrap HTTP status %d", status);
        return false;
    }
    if (!state::init_from_csv(buf, std::strlen(buf)))
    {
        ESP_LOGW(TAG_APP, "Failed to parse bootstrap CSV; proceeding with empty state");
    }
    else
    {
        ESP_LOGI(TAG_APP, "State initialized: %d areas, %d entities",
                 (int)state::areas().size(),
                 (int)state::entities().size());
    }
    ESP_LOGI(TAG_APP, "Bootstrap HTTP ok (status %d)", status);
    return true;
}

static bool run_bootstrap_sequence(void)
{
    static constexpr int kMaxAttempts = 3;
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
    {
        if (!ensure_wifi_connected())
        {
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }
        if (perform_bootstrap_request())
        {
            return true;
        }
        ESP_LOGW(TAG_APP, "Bootstrap attempt %d/%d failed", attempt, kMaxAttempts);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
    return false;
}

extern "C" void app_main(void)
{
    if (wifi_manager_init() != ESP_OK)
    {
        ESP_LOGE(TAG_APP, "WiFi manager init failed");
        return;
    }

    if (!run_bootstrap_sequence())
    {
        ESP_LOGE(TAG_APP, "Bootstrap over HTTPS failed, stopping init");
        return;
    }

    if (devices_init() != ESP_OK)
    {
        return;
    }

    wifi_manager_start_auto(-85, 15000); // Keep Wi-Fi connected in background after bootstrap
    (void)router::start();               // Start connectivity via Router (currently MQTT)

    /* Create your UI under LVGL mutex */
    lvgl_port_lock(0);
    ui_app_init();
    ui_init_screensaver_support();
    lvgl_port_unlock();

    ui_start_weather_polling();
}
