#pragma once

#include "esp_err.h"

namespace transport {

class ITransport {
public:
    using MessageHandler = void(*)(const char* topic, const char* data, int len);
    using ConnectionHandler = void(*)(bool connected);

    virtual ~ITransport() = default;
    virtual esp_err_t start() = 0;
    virtual esp_err_t publish(const char* topic, const char* payload, int qos, bool retain) = 0;
    virtual esp_err_t subscribe(const char* topic, int qos) = 0;
    virtual void set_handler(MessageHandler handler) = 0;
    virtual void set_connection_handler(ConnectionHandler handler) = 0;
    virtual bool is_connected() const = 0;
};

} // namespace transport
