#pragma once

#include <cstdint>

namespace app_config
{

    // Milliseconds of LVGL inactivity before switching to screensaver state.
    constexpr std::uint32_t kScreensaverIdleTimeoutMs = 10000;

    // Milliseconds after entering screensaver before turning off backlight.
    constexpr std::uint32_t kScreensaverBacklightOffDelayMs = 20000;

    // Screensaver clock update period.
    constexpr std::uint32_t kScreensaverClockTickMs = 1000;

    // Interval between weather HTTP polls.
    constexpr std::uint32_t kWeatherPollIntervalMs = 50000;

} // namespace app_config

