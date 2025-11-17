#pragma once

// HTTP bootstrap configuration for local test API.
// Fill in your URL and optional Bearer token here.
// Example URL: "http://192.168.1.100:8080/api/template"

#ifndef HA_HTTP_BOOTSTRAP_URL
#define HA_HTTP_BOOTSTRAP_URL1 "http://192.168.0.105:8080/api/template"
#define HA_HTTP_BOOTSTRAP_URL "http://192.168.1.185:8123/api/template"
#endif

// If your endpoint requires Bearer auth, put the long token here.
// Leave empty ("") if no Authorization header is needed.
#ifndef HA_HTTP_BEARER_TOKEN
#define HA_HTTP_BEARER_TOKEN "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiIyYTRmMDczZTVkMmI0ZWQ5ODhkYTE4NTVmZjc2NGVmOCIsImlhdCI6MTc2MzIwMDYzMiwiZXhwIjoyMDc4NTYwNjMyfQ.ogO8DhTWIcAu4P8E_wuWGL4BF3d-380QMHw2dOxOJNU"
#endif
