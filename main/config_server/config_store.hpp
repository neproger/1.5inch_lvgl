#pragma once

#include "esp_err.h"
#include <cstddef>

// Simple NVS-backed configuration store for Wi-Fi and Home Assistant connection.
namespace config_store
{

    struct WifiAp
    {
        char ssid[32]{};
        char password[64]{};
    };

    struct HaConn
    {
        char host[64]{};
        uint16_t http_port{8123};
        uint16_t mqtt_port{1883};
        char mqtt_username[32]{};
        char mqtt_password[64]{};
        // Long-lived Home Assistant token (can be >180 chars).
        char http_token[256]{}; // long-lived HA token
    };

    // Initialize NVS namespace (must be called before other operations).
    esp_err_t init();

    // Load Wi-Fi AP list into the provided buffer.
    esp_err_t load_wifi(WifiAp *aps, std::size_t max_count, std::size_t &out_count);

    // Replace Wi-Fi AP list with the provided array.
    esp_err_t save_wifi(const WifiAp *aps, std::size_t count);

    // Load HA connections.
    esp_err_t load_ha(HaConn *conns, std::size_t max_count, std::size_t &out_count);

    // Replace HA connection list.
    esp_err_t save_ha(const HaConn *conns, std::size_t count);

    // Return true if there is at least one Wi-Fi AP and one HA connection stored.
    bool has_basic_config();

} // namespace config_store

