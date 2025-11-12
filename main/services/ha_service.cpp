#include "services/ha_service.hpp"
#include "config/config.hpp"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

namespace ha {

namespace {
Service* s_instance = nullptr;
}

esp_err_t Service::start(transport::ITransport& transport, StateCallback callback)
{
    esp_err_t err = config::load();
    if (err != ESP_OK) {
        return err;
    }
    transport_ = &transport;
    callback_ = callback;
    cfg_ = &config::ha();
    s_instance = this;
    transport.set_handler(&Service::handle_message);
    return transport.start();
}

esp_err_t Service::subscribe_entity(const char* entity_id)
{
    if (!transport_ || !entity_id || !cfg_) return ESP_ERR_INVALID_STATE;
    char topic[128];
    std::snprintf(topic, sizeof(topic), "%s%s", cfg_->state_prefix, entity_id);
    topic[sizeof(topic) - 1] = '\0';
    return transport_->subscribe(topic, 1);
}

esp_err_t Service::toggle(const char* entity_id)
{
    if (!transport_ || !entity_id || !cfg_) return ESP_ERR_INVALID_STATE;
    esp_err_t err = transport_->publish(cfg_->toggle_topic, entity_id, 1, false);
    if (err == ESP_OK) {
        ESP_LOGI("ha_service", "Publish toggle %s -> %s", entity_id, cfg_->toggle_topic);
    } else {
        ESP_LOGW("ha_service", "Toggle publish failed (%s) for %s", esp_err_to_name(err), entity_id);
    }
    return err;
}

bool Service::is_connected() const
{
    return transport_ ? transport_->is_connected() : false;
}

void Service::handle_message(const char* topic, const char* data, int len)
{
    if (!topic || !s_instance || !s_instance->callback_ || !s_instance->cfg_) {
        return;
    }
    const char* prefix = s_instance->cfg_->state_prefix;
    size_t prefix_len = std::strlen(prefix);
    if (std::strncmp(topic, prefix, prefix_len) != 0) {
        return;
    }
    const char* entity = topic + prefix_len;
    s_instance->callback_(entity, data, len);
}

} // namespace ha
