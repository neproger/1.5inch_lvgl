#pragma once

// Shared Home Assistant server configuration (fallback IP/host + ports).
// Runtime values are taken from config_store (web UI); these are only defaults.

#ifndef HA_SERVER_HOST
#define HA_SERVER_HOST "192.168.1.100"
#endif

// HTTP port used for /api/template
#ifndef HA_HTTP_PORT
#define HA_HTTP_PORT "8123"
#endif

// MQTT broker port
#ifndef HA_MQTT_PORT
#define HA_MQTT_PORT "1883"
#endif
