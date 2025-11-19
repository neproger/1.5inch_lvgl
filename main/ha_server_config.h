#pragma once

// Shared Home Assistant server configuration (IP/host + ports).
// Override these via build flags or other headers before including if needed.

#ifndef HA_SERVER_HOST
// Default HA/test broker host/IP
#define HA_SERVER_HOST "192.168.0.105"
#define HA_SERVER_HOST1 "192.168.1.185"
#endif

// HTTP port used for /api/template
#ifndef HA_HTTP_PORT
#define HA_HTTP_PORT "8123"
#endif

// MQTT broker port
#ifndef HA_MQTT_PORT
#define HA_MQTT_PORT "1883"
#endif

