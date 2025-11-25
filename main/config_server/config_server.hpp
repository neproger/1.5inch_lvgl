#pragma once

#include "esp_err.h"

namespace config_server
{

    // Start configuration HTTP server (in current Wi‑Fi mode).
    esp_err_t start();

    // Stop configuration HTTP server if running.
    void stop();

} // namespace config_server

