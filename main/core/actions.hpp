#pragma once

namespace core {

enum class ServerEventType { ConnectedChanged /*, StateUpdated */ };
struct ServerEvent {
    ServerEventType type;
    bool connected; // for ConnectedChanged
    // future: const char* entity_id; const char* payload;
};

// Future placeholder for UI actions
enum class UiActionType { SelectEntity /*, Toggle, Wake, Sleep */ };
struct UiAction {
    UiActionType type;
    int index;
};

} // namespace core

