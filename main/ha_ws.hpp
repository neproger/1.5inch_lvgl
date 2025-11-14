#pragma once

#include <cstddef>
#include "esp_err.h"

namespace ha_ws {

using MessageHandler = void (*)(const char *json, size_t len);

// Start WebSocket client (idempotent).
esp_err_t start();

// True when socket is connected and auth_ok received.
bool is_ready();

// Optional low-level connection status helpers.
bool is_connected();
bool is_authenticated();

// Request helpers for HA registries.
esp_err_t request_area_registry_list();
esp_err_t request_device_registry_list();
esp_err_t request_entity_registry_list();

// Register callback for application-level messages (non-auth frames).
void set_message_handler(MessageHandler handler);

} // namespace ha_ws

