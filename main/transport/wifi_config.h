#pragma once

#include <stddef.h>

// Optional compile-time Wi-Fi list (fallback).
// Prefer configuring networks via web UI (config_server).

#define WIFI_KNOWN_AP_MAX 5

typedef struct
{
    const char *ssid;
    const char *password;
} wifi_known_ap_t;

// Default list is empty; add entries here only for local testing.
static const wifi_known_ap_t WIFI_KNOWN_APS[] = {
    // Example:
    // { "MyWiFi", "password123" },
};

static const size_t WIFI_KNOWN_APS_COUNT = sizeof(WIFI_KNOWN_APS) / sizeof(WIFI_KNOWN_APS[0]);

