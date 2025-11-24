#pragma once

#include <cstdint>

namespace http_manager
{

    // Bootstrap Home Assistant state over HTTP template API.
    // Returns true on success, false if all attempts failed.
    bool bootstrap_state();

    // Start periodic weather polling over HTTP.
    // Updates state_manager::set_weather/set_clock() on successful polls.
    void start_weather_polling();

} // namespace http_manager

