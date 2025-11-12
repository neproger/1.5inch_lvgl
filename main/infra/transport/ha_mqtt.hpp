#pragma once

#include <stdbool.h>
#include "esp_err.h"

namespace ha_mqtt {

// Start MQTT client (idempotent). Non-blocking, auto-reconnect enabled.
esp_err_t start();

// Current connection state.
bool is_connected();

// Publish arbitrary payload (null-terminated) to topic.
esp_err_t publish(const char* topic, const char* payload, int qos, bool retain);

// Publish a toggle command with payload = entity_id (plain text).
esp_err_t publish_toggle(const char* entity_id);

// Message/connection handler types
using MessageHandler = void(*)(const char* topic, const char* data, int len);
using ConnectionHandler = void(*)(bool connected);

void set_message_handler(MessageHandler handler);
void set_connection_handler(ConnectionHandler handler);

// Subscribe to a topic (single level). Will be re-subscribed after reconnect.
esp_err_t subscribe(const char* topic, int qos);

} // namespace ha_mqtt
