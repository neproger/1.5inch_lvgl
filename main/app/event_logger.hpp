#pragma once

#include "esp_err.h"

namespace event_logger
{

    // Register a handler on the default ESP event loop
    // that logs every event (any base / any id).
    esp_err_t init();

} // namespace event_logger

