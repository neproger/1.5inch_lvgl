#pragma once

#include <stdbool.h>
#include "esp_err.h"

namespace core {

static constexpr int kMaxEntities = 8;

struct EntitySlot {
    char entity_id[64];
    char value[32];
};

struct AppState {
    int selected;
    bool connected;
    int entity_count;
    EntitySlot entities[kMaxEntities];
};

using StateListener = void(*)(const AppState&);

esp_err_t store_start();
void store_subscribe(StateListener cb);
void store_dispatch_connected(bool connected);
void store_dispatch_entity_state(const char *entity_id, const char *value, int len);
void store_set_selected(int index);
void store_init_entities(const char *const *entity_ids, int count);
const char *store_get_entity_value(int index);

// Optional accessors
const AppState& store_get_state();

} // namespace core
