#include "config_store.hpp"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

namespace config_store
{

    namespace
    {
        const char *TAG = "cfg_store";
        const char *NS = "cfg";

        esp_err_t open_handle(nvs_handle_t &handle)
        {
            esp_err_t err = nvs_open(NS, NVS_READWRITE, &handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
            }
            return err;
        }

        template <typename T>
        esp_err_t load_array(const char *count_key, const char *item_key_prefix, T *items, std::size_t max_count, std::size_t &out_count)
        {
            out_count = 0;
            nvs_handle_t handle{};
            ESP_RETURN_ON_ERROR(open_handle(handle), TAG, "open_handle failed");

            uint32_t count = 0;
            esp_err_t err = nvs_get_u32(handle, count_key, &count);
            if (err == ESP_ERR_NVS_NOT_FOUND)
            {
                nvs_close(handle);
                return ESP_OK;
            }
            ESP_RETURN_ON_ERROR(err, TAG, "nvs_get_u32 count failed");

            if (count > max_count)
            {
                count = static_cast<uint32_t>(max_count);
            }

            for (uint32_t i = 0; i < count; ++i)
            {
                char key[16];
                snprintf(key, sizeof(key), "%s%u", item_key_prefix, i);
                size_t len = sizeof(T);
                err = nvs_get_blob(handle, key, &items[i], &len);
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG, "nvs_get_blob %s failed: %s", key, esp_err_to_name(err));
                    break;
                }
                ++out_count;
            }
            nvs_close(handle);
            return ESP_OK;
        }

        template <typename T>
        esp_err_t save_array(const char *count_key, const char *item_key_prefix, const T *items, std::size_t count)
        {
            nvs_handle_t handle{};
            ESP_RETURN_ON_ERROR(open_handle(handle), TAG, "open_handle failed");

            ESP_RETURN_ON_ERROR(nvs_set_u32(handle, count_key, static_cast<uint32_t>(count)), TAG, "nvs_set_u32 count failed");

            for (std::size_t i = 0; i < count; ++i)
            {
                char key[16];
                snprintf(key, sizeof(key), "%s%u", item_key_prefix, static_cast<unsigned>(i));
                esp_err_t err = nvs_set_blob(handle, key, &items[i], sizeof(T));
                if (err != ESP_OK)
                {
                    ESP_LOGE(TAG, "nvs_set_blob %s failed: %s", key, esp_err_to_name(err));
                    nvs_close(handle);
                    return err;
                }
            }

            esp_err_t err = nvs_commit(handle);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
            }
            nvs_close(handle);
            return err;
        }

    } // namespace

    esp_err_t init()
    {
        // Assume global NVS is already initialized by wifi_manager_init().
        // Nothing to do here yet, but keep for symmetry/extension.
        return ESP_OK;
    }

    esp_err_t load_wifi(WifiAp *aps, std::size_t max_count, std::size_t &out_count)
    {
        return load_array("wifi_count", "wifi_", aps, max_count, out_count);
    }

    esp_err_t save_wifi(const WifiAp *aps, std::size_t count)
    {
        return save_array("wifi_count", "wifi_", aps, count);
    }

    esp_err_t load_ha(HaConn *conns, std::size_t max_count, std::size_t &out_count)
    {
        return load_array("ha_count", "ha_", conns, max_count, out_count);
    }

    esp_err_t save_ha(const HaConn *conns, std::size_t count)
    {
        return save_array("ha_count", "ha_", conns, count);
    }

    bool has_basic_config()
    {
        WifiAp aps[1];
        HaConn ha[1];
        std::size_t wifi_count = 0;
        std::size_t ha_count = 0;
        if (load_wifi(aps, 1, wifi_count) != ESP_OK)
        {
            return false;
        }
        if (load_ha(ha, 1, ha_count) != ESP_OK)
        {
            return false;
        }
        return wifi_count > 0 && ha_count > 0;
    }

} // namespace config_store

