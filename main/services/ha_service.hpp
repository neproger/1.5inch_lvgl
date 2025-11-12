#pragma once

#include "esp_err.h"
#include "infra/transport/i_transport.hpp"
#include "config/config.hpp"

namespace ha {

using StateCallback = void(*)(const char* entity_id, const char* payload, int len);

class Service {
public:
    esp_err_t start(transport::ITransport& transport, StateCallback callback);
    esp_err_t subscribe_entity(const char* entity_id);
    esp_err_t toggle(const char* entity_id);
    bool is_connected() const;

private:
    static void handle_message(const char* topic, const char* data, int len);

    transport::ITransport* transport_ = nullptr;
    StateCallback callback_ = nullptr;
    const config::HaSettings* cfg_ = nullptr;
};

} // namespace ha
