#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

namespace core {

constexpr int kMaxEntities = 8;
constexpr size_t kEntityIdLen = 64;
constexpr size_t kEntityValueLen = 32;

struct EntityState {
    char id[kEntityIdLen];
    char value[kEntityValueLen];
};

struct AppState {
    int selected;
    bool connected;
    int entity_count;
    EntityState entities[kMaxEntities];
};

using StateListener = void(*)(const AppState&);

esp_err_t store_start();
void store_subscribe(StateListener cb);
void store_dispatch_connected(bool connected);
void store_dispatch_state(const char* entity_id, const char* payload, int len);
void store_register_entities(const char* const* entity_ids, int count);

// Optional accessors
const AppState& store_get_state();

} // namespace core

