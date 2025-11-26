#pragma once

#include <cstdint>
#include <string>

namespace http_manager
{

    // Bootstrap Home Assistant state over HTTP template API.
    // Returns true on success, false if all attempts failed.
    bool bootstrap_state();

    // Start periodic weather polling over HTTP.
    // Updates state_manager::set_weather/set_clock() on successful polls.
    void start_weather_polling();

    // Return the last HA HTTP host/port that successfully responded
    // during bootstrap or weather polling. Returns false if no successful
    // HTTP request has been recorded yet.
    bool get_last_successful_http_host(std::string &host, std::uint16_t &http_port);

} // namespace http_manager

