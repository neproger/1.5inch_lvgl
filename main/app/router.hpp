#pragma once

#include "esp_err.h"

namespace router {

// Start underlying connectivity (MQTT for now).
esp_err_t start();

// Current connectivity status.
bool is_connected();

// UI action: toggle entity via server.
esp_err_t toggle(const char* entity_id);

} // namespace router

