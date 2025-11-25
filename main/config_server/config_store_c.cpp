#include "config_store_c.h"

#include "config_store.hpp"

#include <cstring>

extern "C"
{

    esp_err_t config_store_load_wifi_c(config_store_wifi_ap_t *aps, size_t max_count, size_t *out_count)
    {
        if (!aps || !out_count)
        {
            return ESP_ERR_INVALID_ARG;
        }
        config_store::WifiAp tmp[8];
        std::size_t count_cpp = 0;
        const size_t max_cpp = (max_count < 8) ? max_count : 8;
        esp_err_t err = config_store::load_wifi(tmp, max_cpp, count_cpp);
        if (err != ESP_OK)
        {
            *out_count = 0;
            return err;
        }
        if (count_cpp > max_count)
        {
            count_cpp = max_count;
        }
        for (std::size_t i = 0; i < count_cpp; ++i)
        {
            config_store_wifi_ap_t ap{};
            strncpy(ap.ssid, tmp[i].ssid, sizeof(ap.ssid) - 1);
            strncpy(ap.password, tmp[i].password, sizeof(ap.password) - 1);
            aps[i] = ap;
        }
        *out_count = count_cpp;
        return ESP_OK;
    }

    esp_err_t config_store_load_ha_c(config_store_ha_conn_t *conns, size_t max_count, size_t *out_count)
    {
        if (!conns || !out_count)
        {
            return ESP_ERR_INVALID_ARG;
        }
        config_store::HaConn tmp[4];
        std::size_t count_cpp = 0;
        const size_t max_cpp = (max_count < 4) ? max_count : 4;
        esp_err_t err = config_store::load_ha(tmp, max_cpp, count_cpp);
        if (err != ESP_OK)
        {
            *out_count = 0;
            return err;
        }
        if (count_cpp > max_count)
        {
            count_cpp = max_count;
        }
        for (std::size_t i = 0; i < count_cpp; ++i)
        {
            config_store_ha_conn_t c{};
            strncpy(c.host, tmp[i].host, sizeof(c.host) - 1);
            c.http_port = tmp[i].http_port;
            c.mqtt_port = tmp[i].mqtt_port;
            strncpy(c.mqtt_username, tmp[i].mqtt_username, sizeof(c.mqtt_username) - 1);
            strncpy(c.mqtt_password, tmp[i].mqtt_password, sizeof(c.mqtt_password) - 1);
            strncpy(c.http_token, tmp[i].http_token, sizeof(c.http_token) - 1);
            conns[i] = c;
        }
        *out_count = count_cpp;
        return ESP_OK;
    }

} // extern "C"
