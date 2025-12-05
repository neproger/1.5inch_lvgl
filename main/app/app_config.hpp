#pragma once

#include <cstdint>

namespace app_config
{

    // Accent/theme colors (RGB888 hex).
    constexpr std::uint32_t kThemePrimaryColorHex = 0x006aa3;
    constexpr std::uint32_t kThemeSecondaryColorHex = 0x303030;

    // Milliseconds of LVGL inactivity before switching to screensaver state.
    constexpr std::uint32_t kScreensaverIdleTimeoutMs = 10000;

    // Milliseconds after entering screensaver before turning off backlight.
    constexpr std::uint32_t kScreensaverBacklightOffDelayMs = 20000;

    // Screensaver clock update period.
    constexpr std::uint32_t kScreensaverClockTickMs = 1000;

    // Interval between weather HTTP polls.
    constexpr std::uint32_t kWeatherPollIntervalMs = 50000;

} // namespace app_config
