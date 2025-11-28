#pragma once

// Global application state enum used by the FSM in main.cpp
// and in app_events payloads.

enum class AppState
{
    BootDevices,
    BootWifi,
    BootBootstrap,
    NormalAwake,
    NormalScreensaver,
    NormalSleep,
    ConfigMode,
};

