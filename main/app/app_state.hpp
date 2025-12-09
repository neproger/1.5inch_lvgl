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
    ConfigMode,
};

// Global application state variable defined in main.cpp.
extern AppState g_app_state;

