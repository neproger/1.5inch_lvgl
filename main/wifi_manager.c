#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "wifi_manager.h"
#include "wifi_config.h"

static const char *TAG = "wifi_mgr";

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_netif = NULL;

static const int WIFI_CONNECTED_BIT = BIT0;
static const int WIFI_GOT_IP_BIT    = BIT1;

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "STA started");
            break;
        case WIFI_EVENT_STA_CONNECTED:
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            ESP_LOGI(TAG, "Connected to AP");
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            xEventGroupClearBits(s_wifi_event_group, WIFI_GOT_IP_BIT);
            {
                wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t *)event_data;
                ESP_LOGW(TAG, "Disconnected (reason=%d)", ev ? ev->reason : -1);
            }
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_GOT_IP_BIT);
    }
}

static esp_err_t ensure_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

esp_err_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(ensure_nvs_init());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}

static int find_known_index(const char *ssid)
{
    for (size_t i = 0; i < WIFI_KNOWN_APS_COUNT; ++i) {
        if (strcmp(WIFI_KNOWN_APS[i].ssid, ssid) == 0) {
            return (int)i;
        }
    }
    return -1;
}

esp_err_t wifi_manager_connect_best_known(int32_t min_rssi)
{
    if (WIFI_KNOWN_APS_COUNT == 0) {
        ESP_LOGW(TAG, "No known APs configured");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_scan_config_t scan_cfg = {
        .ssid = 0,
        .bssid = 0,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 300 } },
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_cfg, true /* block */));

    uint16_t ap_num = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));
    if (ap_num == 0) {
        ESP_LOGW(TAG, "Scan complete: no APs found");
        return ESP_ERR_NOT_FOUND;
    }

    wifi_ap_record_t *list = calloc(ap_num, sizeof(wifi_ap_record_t));
    if (!list) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, list));

    int best_idx_in_list = -1;
    int best_known_idx = -1;
    int32_t best_rssi = -127;

    for (int i = 0; i < ap_num; ++i) {
        int known_idx = find_known_index((const char *)list[i].ssid);
        if (known_idx >= 0) {
            ESP_LOGI(TAG, "Seen known AP '%s' RSSI=%d", list[i].ssid, list[i].rssi);
            if (list[i].rssi > best_rssi) {
                best_rssi = list[i].rssi;
                best_idx_in_list = i;
                best_known_idx = known_idx;
            }
        }
    }

    if (best_idx_in_list < 0) {
        ESP_LOGW(TAG, "No known APs in range");
        free(list);
        return ESP_ERR_NOT_FOUND;
    }

    if (best_rssi < min_rssi) {
        ESP_LOGW(TAG, "Best known AP too weak: RSSI=%d < %d", best_rssi, min_rssi);
        free(list);
        return ESP_ERR_INVALID_RESPONSE;
    }

    wifi_config_t cfg = { 0 };
    strncpy((char *)cfg.sta.ssid, (const char *)list[best_idx_in_list].ssid, sizeof(cfg.sta.ssid));
    strncpy((char *)cfg.sta.password, WIFI_KNOWN_APS[best_known_idx].password, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; // reasonable default; will still connect to stronger auth
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;

    ESP_LOGI(TAG, "Connecting to '%s'...", cfg.sta.ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_connect());

    free(list);
    return ESP_OK;
}

bool wifi_manager_wait_ip(int wait_ms)
{
    if (!s_wifi_event_group) return false;
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_GOT_IP_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(wait_ms));
    return (bits & WIFI_GOT_IP_BIT) != 0;
}

