#include "app/entities.hpp"

namespace app {

const EntityDesc g_entities[] = {
    {"Свет на кухне", "switch.wifi_breaker_t_switch_1"},
    {"Свет в коридоре", "switch.wifi_breaker_t_switch_2"},
};

const int g_entity_count = sizeof(g_entities) / sizeof(g_entities[0]);

} // namespace app

