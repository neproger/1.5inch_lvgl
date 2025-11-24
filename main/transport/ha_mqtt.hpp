#pragma once

#include <stdbool.h>
#include "esp_err.h"

namespace ha_mqtt {

// Start MQTT client (idempotent). Non-blocking, auto-reconnect enabled.
esp_err_t start();

// Current connection state.
bool is_connected();

// Publish a toggle command with payload = entity_id (plain text).
esp_err_t publish_toggle(const char* entity_id);

// Message handler type: topic (null-terminated), data pointer and length
using MessageHandler = void(*)(const char* topic, const char* data, int len);

// Set a global message handler invoked for every incoming MQTT message.
void set_message_handler(MessageHandler handler);

// Subscribe to a topic (single level). Will be re-subscribed after reconnect.
esp_err_t subscribe(const char* topic, int qos);

} // namespace ha_mqtt
