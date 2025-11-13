#pragma once

namespace app {

struct EntityDesc {
    const char *name;
    const char *entity_id;
};

extern const EntityDesc g_entities[];
extern const int g_entity_count;

} // namespace app

