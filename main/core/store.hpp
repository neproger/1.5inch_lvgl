#pragma once

#include <stdbool.h>
#include "esp_err.h"

namespace core {

struct AppState {
    int selected;
    bool connected;
};

using StateListener = void(*)(const AppState&);

esp_err_t store_start();
void store_subscribe(StateListener cb);
void store_dispatch_connected(bool connected);

// Optional accessors
const AppState& store_get_state();

} // namespace core

