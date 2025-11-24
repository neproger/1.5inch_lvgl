#pragma once

#include <stddef.h>

// Простой конфиг известных точек доступа.
// Заполните SSID/PASS под себя.

#define WIFI_KNOWN_AP_MAX 5

typedef struct {
    const char *ssid;
    const char *password;
} wifi_known_ap_t;

// Пример. Меняйте по нужде. Можно добавлять/удалять записи.
static const wifi_known_ap_t WIFI_KNOWN_APS[] = {
    { "MERCUSYS_0348", "18188530" },
    { "ITPod", "zxcxzzxc" },
    { "oneplus8", "zxcxzzxc" }
    // { "PhoneHotspot", "hotspot_password" },
};

static const size_t WIFI_KNOWN_APS_COUNT = sizeof(WIFI_KNOWN_APS) / sizeof(WIFI_KNOWN_APS[0]);

