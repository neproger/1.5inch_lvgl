#include "app/router.hpp"
#include "core/store.hpp"
#include "config/config.hpp"
#include "infra/transport/mqtt_transport.hpp"
#include "services/ha_service.hpp"

namespace {
transport::MqttTransport s_transport;
ha::Service s_service;
const config::HaSettings* s_cfg = nullptr;

void on_state_update(const char* entity_id, const char* payload, int len)
{
    core::store_dispatch_state(entity_id, payload, len);
}

void subscribe_entities()
{
    if (!s_cfg) return;
    for (int i = 0; i < s_cfg->entity_count; ++i) {
        (void)s_service.subscribe_entity(s_cfg->entities[i].entity_id);
    }
}

void on_connection_changed(bool connected)
{
    core::store_dispatch_connected(connected);
}
}

namespace router {

esp_err_t start()
{
    // For now delegate to MQTT; later can wire Store/Services here.
    esp_err_t err = core::store_start();
    if (err != ESP_OK) return err;
    err = config::load();
    if (err != ESP_OK) return err;
    s_cfg = &config::ha();

    if (s_cfg->entity_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const char* ids[config::kMaxEntities] = {};
    for (int i = 0; i < s_cfg->entity_count; ++i) {
        ids[i] = s_cfg->entities[i].entity_id;
    }
    core::store_register_entities(ids, s_cfg->entity_count);

    s_transport.set_connection_handler(&on_connection_changed);
    err = s_service.start(s_transport, &on_state_update);
    if (err != ESP_OK) return err;
    core::store_dispatch_connected(s_service.is_connected());
    subscribe_entities();
    return ESP_OK;
}

bool is_connected()
{
    return s_service.is_connected();
}

esp_err_t toggle(const char* entity_id)
{
    return s_service.toggle(entity_id);
}

} // namespace router
