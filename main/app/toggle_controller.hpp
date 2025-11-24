#pragma once

#include "esp_err.h"

namespace toggle_controller
{

    // Initialize MQTT toggle handling on the application event bus.
    // Listens for TOGGLE_REQUEST and performs router::toggle,
    // then publishes TOGGLE_RESULT.
    esp_err_t init();

} // namespace toggle_controller

