#pragma once

#include "esp_err.h"

namespace config {

constexpr int kMaxEntities = 4;

struct Entity {
    char name[32];
    char entity_id[64];
    char type[16];
};

struct HaSettings {
    char broker_uri[128];
    char username[64];
    char password[64];
    char client_id[64];
    char base_topic[64];
    char status_topic[96];
    char toggle_topic[96];
    char state_prefix[96];
    int entity_count;
    Entity entities[kMaxEntities];
};

esp_err_t load();
const HaSettings& ha();

} // namespace config
