#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct
    {
        char ssid[32];
        char password[64];
    } config_store_wifi_ap_t;

    typedef struct
    {
        char host[64];
        uint16_t http_port;
        uint16_t mqtt_port;
        char mqtt_username[32];
        char mqtt_password[64];
        char http_token[160];
    } config_store_ha_conn_t;

    esp_err_t config_store_load_wifi_c(config_store_wifi_ap_t *aps, size_t max_count, size_t *out_count);
    esp_err_t config_store_load_ha_c(config_store_ha_conn_t *conns, size_t max_count, size_t *out_count);

#ifdef __cplusplus
}
#endif

