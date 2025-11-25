#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_check.h"

#include "wifi_manager.h"
#include "wifi_config.h"
#include "config_server/config_store_c.h"

static const char *TAG = "wifi_mgr";

// Internal state for Wi‑Fi manager (kept in a single struct instead of many globals)
typedef struct
{
    EventGroupHandle_t wifi_event_group;
    esp_netif_t *netif;
    esp_netif_t *ap_netif;
    TaskHandle_t mgr_task;
    int32_t min_rssi;
    int scan_interval_ms;
} wifi_manager_state_t;

static wifi_manager_state_t s_state = {
    .wifi_event_group = NULL,
    .netif = NULL,
    .ap_netif = NULL,
    .mgr_task = NULL,
    .min_rssi = -85,
    .scan_interval_ms = 15000,
};

static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_GOT_IP_BIT = BIT1;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            xEventGroupSetBits(s_state.wifi_event_group, WIFI_CONNECTED_BIT);
            ESP_LOGI(TAG, "Connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits(s_state.wifi_event_group, WIFI_CONNECTED_BIT);
            xEventGroupClearBits(s_state.wifi_event_group, WIFI_GOT_IP_BIT);
            if (event_data)
            {
                const wifi_event_sta_disconnected_t *ev = (const wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "Disconnected (reason=%d)", ev->reason);
            }
            else
            {
                ESP_LOGW(TAG, "Disconnected (no event data)");
            }
            break;
        default:
            break;
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_state.wifi_event_group, WIFI_GOT_IP_BIT);
    }
}

static esp_err_t ensure_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_LOGW(TAG, "NVS needs erase, reinitializing");
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "NVS erase failed");
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t wifi_manager_init(void)
{
    ESP_RETURN_ON_ERROR(ensure_nvs_init(), TAG, "NVS init failed");

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop create failed");

    if (!s_state.netif)
    {
        s_state.netif = esp_netif_create_default_wifi_sta();
        if (!s_state.netif)
        {
            ESP_LOGE(TAG, "Failed to create default WiFi STA netif");
            return ESP_ERR_NO_MEM;
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "esp_wifi_init failed");

    if (!s_state.wifi_event_group)
    {
        s_state.wifi_event_group = xEventGroupCreate();
        if (!s_state.wifi_event_group)
        {
            ESP_LOGE(TAG, "Failed to create WiFi event group");
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL),
                        TAG, "register WIFI_EVENT handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(
                            IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL),
                        TAG, "register IP_EVENT handler failed");

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "esp_wifi_set_mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "esp_wifi_start failed");

    // Disable Wi‑Fi power save to reduce latency/timeouts
    esp_wifi_set_ps(WIFI_PS_NONE);

    return ESP_OK;
}

static int find_known_index_config(const char *ssid, const config_store_wifi_ap_t *aps, size_t count)
{
    for (size_t i = 0; i < count; ++i)
    {
        if (strcmp(aps[i].ssid, ssid) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

static int find_known_index_static(const char *ssid)
{
    for (size_t i = 0; i < WIFI_KNOWN_APS_COUNT; ++i)
    {
        if (strcmp(WIFI_KNOWN_APS[i].ssid, ssid) == 0)
        {
            return (int)i;
        }
    }
    return -1;
}

esp_err_t wifi_manager_connect_best_known(int32_t min_rssi)
{
    config_store_wifi_ap_t cfg_aps[WIFI_KNOWN_AP_MAX];
    size_t cfg_count = 0;
    bool use_cfg = false;

    if (config_store_load_wifi_c(cfg_aps, WIFI_KNOWN_AP_MAX, &cfg_count) == ESP_OK && cfg_count > 0)
    {
        use_cfg = true;
        ESP_LOGI(TAG, "Using %u Wi-Fi APs from config_store", (unsigned)cfg_count);
    }
    else if (WIFI_KNOWN_APS_COUNT > 0)
    {
        ESP_LOGI(TAG, "Using compile-time Wi-Fi AP list");
    }
    else
    {
        ESP_LOGW(TAG, "No known APs configured (config_store and wifi_config empty)");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = {.active = {.min = 100, .max = 300}},
    };

    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&scan_cfg, true /* block */), TAG, "scan start failed");

    uint16_t ap_num = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&ap_num), TAG, "scan get_ap_num failed");
    if (ap_num == 0)
    {
        ESP_LOGW(TAG, "Scan complete: no APs found");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_ap_record_t *list = (wifi_ap_record_t *)calloc(ap_num, sizeof(wifi_ap_record_t));
    if (!list)
    {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_wifi_scan_get_ap_records(&ap_num, list);
    if (err != ESP_OK)
    {
        free(list);
        ESP_LOGW(TAG, "scan get_ap_records failed: %s", esp_err_to_name(err));
        return err;
    }

    int best_idx_in_list = -1;
    int best_known_idx = -1;
    int32_t best_rssi = -127;

    for (int i = 0; i < ap_num; ++i)
    {
        int known_idx = -1;
        if (use_cfg)
        {
            known_idx = find_known_index_config((const char *)list[i].ssid, cfg_aps, cfg_count);
        }
        else
        {
            known_idx = find_known_index_static((const char *)list[i].ssid);
        }
        if (known_idx >= 0)
        {
            ESP_LOGI(TAG, "Seen known AP '%s' RSSI=%d", list[i].ssid, list[i].rssi);
            if (list[i].rssi > best_rssi)
            {
                best_rssi = list[i].rssi;
                best_idx_in_list = i;
                best_known_idx = known_idx;
            }
        }
    }

    if (best_idx_in_list < 0)
    {
        ESP_LOGW(TAG, "No known APs in range");
        free(list);
        return ESP_ERR_NOT_FOUND;
    }

    if (best_rssi < min_rssi)
    {
        ESP_LOGW(TAG, "Best known AP too weak: RSSI=%ld < %ld", (long)best_rssi, (long)min_rssi);
        free(list);
        return ESP_ERR_INVALID_RESPONSE;
    }

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.sta.ssid, (const char *)list[best_idx_in_list].ssid, sizeof(cfg.sta.ssid));
    cfg.sta.ssid[sizeof(cfg.sta.ssid) - 1] = '\0';

    if (use_cfg)
    {
        strncpy((char *)cfg.sta.password, cfg_aps[best_known_idx].password, sizeof(cfg.sta.password));
    }
    else
    {
        strncpy((char *)cfg.sta.password, WIFI_KNOWN_APS[best_known_idx].password, sizeof(cfg.sta.password));
    }
    cfg.sta.password[sizeof(cfg.sta.password) - 1] = '\0';

    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // reasonable default; will still connect to stronger auth
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Connecting to '%s'...", cfg.sta.ssid);
    free(list);

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &cfg), TAG, "esp_wifi_set_config failed");
    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "esp_wifi_connect failed");

    return ESP_OK;
}

bool wifi_manager_wait_ip(int wait_ms)
{
    if (!s_state.wifi_event_group)
    {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_state.wifi_event_group,
                                            WIFI_GOT_IP_BIT,
                                            pdFALSE,
                                            pdTRUE,
                                            pdMS_TO_TICKS(wait_ms));
    return (bits & WIFI_GOT_IP_BIT) != 0;
}

bool wifi_manager_is_connected(void)
{
    if (!s_state.wifi_event_group)
    {
        return false;
    }
    EventBits_t bits = xEventGroupGetBits(s_state.wifi_event_group);
    return (bits & WIFI_GOT_IP_BIT) != 0;
}

static void wifi_mgr_task(void *arg)
{
    (void)arg;
    for (;;)
    {
        if (!wifi_manager_is_connected())
        {
            esp_err_t r = wifi_manager_connect_best_known(s_state.min_rssi);
            if (r == ESP_OK)
            {
                // Successfully initiated connection; wait a bit for IP acquisition.
                (void)wifi_manager_wait_ip(10000);
            }
            vTaskDelay(pdMS_TO_TICKS(s_state.scan_interval_ms));
        }
        else
        {
            // Already connected: just sleep and periodically re-check status.
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

void wifi_manager_start_auto(int32_t min_rssi, int scan_interval_ms)
{
    s_state.min_rssi = min_rssi;
    s_state.scan_interval_ms = (scan_interval_ms <= 0) ? 15000 : scan_interval_ms;
    if (!s_state.mgr_task)
    {
        xTaskCreate(wifi_mgr_task, "wifi_mgr", 4096, NULL, 4, &s_state.mgr_task);
    }
}

esp_err_t wifi_manager_start_ap_config(const char *ssid, const char *password)
{
    if (!ssid || !*ssid)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_state.netif)
    {
        ESP_LOGE(TAG, "WiFi not initialized, call wifi_manager_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_state.ap_netif)
    {
        s_state.ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_state.ap_netif)
        {
            ESP_LOGE(TAG, "Failed to create AP netif");
            return ESP_ERR_NO_MEM;
        }
    }

    wifi_config_t cfg = {0};
    strncpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid[sizeof(cfg.ap.ssid) - 1] = '\0';
    cfg.ap.ssid_len = (uint8_t)strlen((const char *)cfg.ap.ssid);

    if (password && *password)
    {
        strncpy((char *)cfg.ap.password, password, sizeof(cfg.ap.password));
        cfg.ap.password[sizeof(cfg.ap.password) - 1] = '\0';
        cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    }
    else
    {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
    }

    cfg.ap.max_connection = 4;
    cfg.ap.channel = 1;

    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "esp_wifi_set_mode(AP) failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &cfg), TAG, "esp_wifi_set_config(AP) failed");

    ESP_LOGI(TAG, "AP config started, SSID='%s'", cfg.ap.ssid);
    return ESP_OK;
}
