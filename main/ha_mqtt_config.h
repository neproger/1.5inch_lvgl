#pragma once

// Default MQTT configuration for Home Assistant / Mosquitto
// Override by defining these before including, or add your own init call.

#ifndef HA_USE_MQTT
#define HA_USE_MQTT 1
#endif

// Example: "mqtt://192.168.1.185:1883" or "mqtts://host:8883"

#define HA_MQTT_URI "mqtt://192.168.1.185:1883"
#define HA_MQTT_URI1 "mqtt://192.168.0.105:1883"


// If your broker requires authentication, set these; empty means no auth in URI
#ifndef HA_MQTT_USERNAME
#define HA_MQTT_USERNAME "mqtt_user"
#endif

#ifndef HA_MQTT_PASSWORD
#define HA_MQTT_PASSWORD "mqtt_user_123"
#endif

// Client identity and base topics
#ifndef HA_MQTT_CLIENT_ID
#define HA_MQTT_CLIENT_ID "esp32-kazdev-ui"
#endif

#ifndef HA_MQTT_BASE_TOPIC
#define HA_MQTT_BASE_TOPIC "ha"
#endif

// LWT/birth topics
#ifndef HA_MQTT_STATUS_TOPIC
#define HA_MQTT_STATUS_TOPIC HA_MQTT_BASE_TOPIC "/ui/status"
#endif

// Command topic for generic service proxy (handled by HA automation)
// Payload: entity_id as plain text (e.g., "switch.wifi_breaker_t_switch_1")
#ifndef HA_MQTT_CMD_TOGGLE_TOPIC
#define HA_MQTT_CMD_TOGGLE_TOPIC HA_MQTT_BASE_TOPIC "/cmd/toggle"
#endif

