#pragma once

// HTTP bootstrap configuration for local test API.
// Fill in your URL and optional Bearer token her// Example URL (derived from HA_SERVER_HOST/HA_HTTP_PORT):
// "http://192.168.1.100:8080/api/template"

#include "ha_server_config.h"

// If needed, HA_HTTP_BOOTSTRAP_URL can be overridden before including this header.
#ifndef HA_HTTP_BOOTSTRAP_URL
#define HA_HTTP_BOOTSTRAP_URL "http://" HA_SERVER_HOST ":" HA_HTTP_PORT "/api/template"
#endif

// If your endpoint requires Bearer auth, put the long token here.
// Leave empty ("") if no Authorization header is needed.
#ifndef HA_HTTP_BEARER_TOKEN
#define HA_HTTP_BEARER_TOKEN "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiIyYTRmMDczZTVkMmI0ZWQ5ODhkYTE4NTVmZjc2NGVmOCIsImlhdCI6MTc2MzIwMDYzMiwiZXhwIjoyMDc4NTYwNjMyfQ.ogO8DhTWIcAu4P8E_wuWGL4BF3d-380QMHw2dOxOJNU"
#endif
