#include "core/store.hpp"
#include "core/actions.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstring>

namespace core {

static AppState s_state{};

static StateListener s_listeners[4];
static int s_listener_count = 0;

enum class EvType { ConnectedChanged, StateUpdated };
struct Ev {
    EvType t;
    union {
        bool connected;
        struct {
            char entity_id[sizeof(EntityState::id)];
            char payload[sizeof(EntityState::value)];
        } state;
    };
};

static QueueHandle_t s_q = nullptr;
static TaskHandle_t s_task = nullptr;

static void notify()
{
    for (int i = 0; i < s_listener_count; ++i) {
        if (s_listeners[i]) s_listeners[i](s_state);
    }
}

static int find_entity(const char* id)
{
    for (int i = 0; i < s_state.entity_count; ++i) {
        if (std::strncmp(s_state.entities[i].id, id, sizeof(EntityState::id)) == 0) {
            return i;
        }
    }
    return -1;
}

static bool update_entity_state(const Ev& ev)
{
    const char* id = ev.state.entity_id;
    const char* payload = ev.state.payload;
    if (!id[0]) return false;

    bool modified = false;
    int idx = find_entity(id);
    if (idx < 0 && s_state.entity_count < kMaxEntities) {
        idx = s_state.entity_count++;
        std::strncpy(s_state.entities[idx].id, id, sizeof(EntityState::id) - 1);
        s_state.entities[idx].id[sizeof(EntityState::id) - 1] = '\0';
        s_state.entities[idx].value[0] = '\0';
        modified = true;
    }
    if (idx < 0) return false;

    char* dst = s_state.entities[idx].value;
    if (std::strncmp(dst, payload, sizeof(EntityState::value)) != 0) {
        std::strncpy(dst, payload, sizeof(EntityState::value) - 1);
        dst[sizeof(EntityState::value) - 1] = '\0';
        modified = true;
    }
    return modified;
}

static void store_task(void* /*arg*/)
{
    Ev ev{};
    while (true) {
        if (xQueueReceive(s_q, &ev, portMAX_DELAY) == pdTRUE) {
            bool changed = false;
            switch (ev.t) {
            case EvType::ConnectedChanged:
                if (s_state.connected != ev.connected) {
                    s_state.connected = ev.connected;
                    changed = true;
                }
                break;
            case EvType::StateUpdated:
                changed = update_entity_state(ev);
                break;
            }
            if (changed) {
                notify();
            }
        }
    }
}

esp_err_t store_start()
{
    if (!s_q) s_q = xQueueCreate(16, sizeof(Ev));
    if (!s_q) return ESP_ERR_NO_MEM;
    if (!s_task) {
        if (xTaskCreate(store_task, "store", 3072, nullptr, 4, &s_task) != pdPASS)
            return ESP_FAIL;
    }
    return ESP_OK;
}

void store_subscribe(StateListener cb)
{
    if (!cb) return;
    if (s_listener_count < (int)(sizeof(s_listeners)/sizeof(s_listeners[0]))) {
        s_listeners[s_listener_count++] = cb;
    }
    cb(s_state);
}

void store_dispatch_connected(bool connected)
{
    if (!s_q) return;
    Ev ev{};
    ev.t = EvType::ConnectedChanged;
    ev.connected = connected;
    (void)xQueueSend(s_q, &ev, 0);
}

void store_dispatch_state(const char* entity_id, const char* payload, int len)
{
    if (!s_q || !entity_id) return;
    Ev ev{};
    ev.t = EvType::StateUpdated;
    std::strncpy(ev.state.entity_id, entity_id, sizeof(ev.state.entity_id) - 1);
    ev.state.entity_id[sizeof(ev.state.entity_id) - 1] = '\0';
    if (payload && len > 0) {
        int copy = len;
        if (copy >= (int)sizeof(ev.state.payload)) copy = sizeof(ev.state.payload) - 1;
        std::memcpy(ev.state.payload, payload, copy);
        ev.state.payload[copy] = '\0';
    } else {
        ev.state.payload[0] = '\0';
    }
    (void)xQueueSend(s_q, &ev, 0);
}

void store_register_entities(const char* const* entity_ids, int count)
{
    if (!entity_ids) return;
    int limit = count;
    if (limit > kMaxEntities) limit = kMaxEntities;
    s_state.entity_count = limit;
    for (int i = 0; i < limit; ++i) {
        const char* id = entity_ids[i] ? entity_ids[i] : "";
        std::strncpy(s_state.entities[i].id, id, sizeof(EntityState::id) - 1);
        s_state.entities[i].id[sizeof(EntityState::id) - 1] = '\0';
        s_state.entities[i].value[0] = '\0';
    }
    notify();
}

const AppState& store_get_state()
{
    return s_state;
}

} // namespace core
