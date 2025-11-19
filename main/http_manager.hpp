#pragma once

#include <cstdint>

namespace http_manager
{

    // Bootstrap Home Assistant state over HTTP template API.
    // Returns true on success, false if all attempts failed.
    bool bootstrap_state();

    // Start periodic weather polling over HTTP.
    // on_weather_updated is called from the polling task after state::set_weather()
    // on successful updates; the callback must be fast and handle its own
    // synchronization (e.g. LVGL locks) if it touches UI.
    void start_weather_polling(void (*on_weather_updated)(void));

} // namespace http_manager

