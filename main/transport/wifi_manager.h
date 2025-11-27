#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Wi‑Fi manager API
    esp_err_t wifi_manager_init(void);
    esp_err_t wifi_manager_connect_best_known(int32_t min_rssi);
    bool wifi_manager_wait_ip(int wait_ms);
    void wifi_manager_start_auto(int32_t min_rssi, int scan_interval_ms);

    // Light-sleep helpers (stop/start Wi-Fi STA).
    esp_err_t wifi_manager_suspend(void);
    esp_err_t wifi_manager_resume(void);

    // Start access point for configuration (AP-only mode).
    esp_err_t wifi_manager_start_ap_config(const char *ssid, const char *password);
    bool wifi_manager_is_connected(void);

#ifdef __cplusplus
}
#endif
