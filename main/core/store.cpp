#include "core/store.hpp"
#include "core/actions.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

namespace core {

static AppState s_state{0, false};

static StateListener s_listeners[4];
static int s_listener_count = 0;

enum class EvType { ConnectedChanged };
struct Ev { EvType t; bool connected; };

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
    }
}

void store_dispatch_connected(bool connected)
{
    if (!s_q) return;
    Ev ev{EvType::ConnectedChanged, connected};
    (void)xQueueSend(s_q, &ev, 0);
}

const AppState& store_get_state()
{
    return s_state;
}

} // namespace core

