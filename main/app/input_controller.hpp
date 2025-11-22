#pragma once

#include "esp_err.h"

namespace input_controller
{

    // Initialize mapping from raw input events (knob/button/gesture)
    // to higher-level application events (navigate, wake screensaver, toggle).
    esp_err_t init();

} // namespace input_controller

