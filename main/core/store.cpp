#include "core/store.hpp"
#include "core/actions.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

namespace core {

static AppState s_state{};

static StateListener s_listeners[4];
static int s_listener_count = 0;

enum class EvType { ConnectedChanged, EntityState, SelectedChanged };
struct Ev {
    EvType t;
    bool connected;
    int selected;
    char entity_id[64];
    char value[32];
};

static QueueHandle_t s_q = nullptr;
static TaskHandle_t s_task = nullptr;

static void notify()
{
    for (int i = 0; i < s_listener_count; ++i) {
        if (s_listeners[i]) s_listeners[i](s_state);
    }
}

static void store_task(void* /*arg*/)
{
    Ev ev{};
    while (true) {
        if (xQueueReceive(s_q, &ev, portMAX_DELAY) == pdTRUE) {
            switch (ev.t) {
            case EvType::ConnectedChanged:
                if (s_state.connected != ev.connected) {
                    s_state.connected = ev.connected;
                    notify();
                }
                break;
            case EvType::EntityState:
                for (int i = 0; i < s_state.entity_count; ++i) {
                    if (strcmp(s_state.entities[i].entity_id, ev.entity_id) == 0) {
                        if (strcmp(s_state.entities[i].value, ev.value) != 0) {
                            strncpy(s_state.entities[i].value, ev.value, sizeof(s_state.entities[i].value) - 1);
                            s_state.entities[i].value[sizeof(s_state.entities[i].value) - 1] = '\0';
                            notify();
                        }
                        break;
                    }
                }
                break;
            case EvType::SelectedChanged:
                if (s_state.selected != ev.selected) {
                    s_state.selected = ev.selected;
                    notify();
                }
                break;
            }
        }
    }
}

esp_err_t store_start()
{
    if (!s_q) s_q = xQueueCreate(8, sizeof(Ev));
    if (!s_q) return ESP_ERR_NO_MEM;
    if (!s_task) {
        if (xTaskCreate(store_task, "store", 3072, nullptr, 4, &s_task) != pdPASS)
            return ESP_FAIL;
    }
    return ESP_OK;
}

void store_subscribe(StateListener cb)
{
    if (s_listener_count < (int)(sizeof(s_listeners)/sizeof(s_listeners[0]))) {
        s_listeners[s_listener_count++] = cb;
        if (cb) {
            cb(s_state);
        }
    }
}

void store_dispatch_connected(bool connected)
{
    if (!s_q) return;
    Ev ev{EvType::ConnectedChanged, connected};
    (void)xQueueSend(s_q, &ev, 0);
}

void store_dispatch_entity_state(const char *entity_id, const char *value, int len)
{
    if (!s_q || !entity_id) return;
    Ev ev{};
    ev.t = EvType::EntityState;
    strncpy(ev.entity_id, entity_id, sizeof(ev.entity_id) - 1);
    ev.entity_id[sizeof(ev.entity_id) - 1] = '\0';
    if (!value) value = "";
    if (len < 0) len = (int)strlen(value);
    if (len >= (int)sizeof(ev.value)) len = sizeof(ev.value) - 1;
    memcpy(ev.value, value, len);
    ev.value[len] = '\0';
    (void)xQueueSend(s_q, &ev, 0);
}

void store_set_selected(int index)
{
    if (!s_q) return;
    Ev ev{};
    ev.t = EvType::SelectedChanged;
    ev.selected = index;
    (void)xQueueSend(s_q, &ev, 0);
}

void store_init_entities(const char *const *entity_ids, int count)
{
    if (!entity_ids) return;
    if (count > kMaxEntities) count = kMaxEntities;
    s_state.entity_count = count;
    for (int i = 0; i < count; ++i) {
        strncpy(s_state.entities[i].entity_id, entity_ids[i], sizeof(s_state.entities[i].entity_id) - 1);
        s_state.entities[i].entity_id[sizeof(s_state.entities[i].entity_id) - 1] = '\0';
        s_state.entities[i].value[0] = '\0';
    }
    notify();
}

const char *store_get_entity_value(int index)
{
    if (index < 0 || index >= s_state.entity_count) return "";
    return s_state.entities[index].value;
}

const AppState& store_get_state()
{
    return s_state;
}

} // namespace core
