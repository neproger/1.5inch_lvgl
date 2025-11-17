#pragma once

#include <vector>
#include <string>
#include <functional>

namespace state {

struct Area {
    std::string id;
    std::string name;
};

struct Entity {
    std::string id;
    std::string name;
    std::string state;
    std::string area_id;
};

using EntityListener = std::function<void(const Entity &)>;

// Parse initial state from CSV (bootstrap HTTP response).
// Returns true on success, false on parse error.
bool init_from_csv(const char *csv, size_t len);

// Update entity state by ID; notifies listeners if value changed.
bool set_entity_state(const std::string &entity_id, const std::string &state);

// Accessors
const std::vector<Area> &areas();
const std::vector<Entity> &entities();
const Entity *find_entity(const std::string &id);

// UI subscriptions
int subscribe_entity(const std::string &id, EntityListener cb);
void unsubscribe(int subscription_id);

} // namespace state

