#pragma once

#include "infra/transport/i_transport.hpp"

namespace transport {

class MqttTransport : public ITransport {
public:
    esp_err_t start() override;
    esp_err_t publish(const char* topic, const char* payload, int qos, bool retain) override;
    esp_err_t subscribe(const char* topic, int qos) override;
    void set_handler(MessageHandler handler) override;
    void set_connection_handler(ConnectionHandler handler) override;
    bool is_connected() const override;

private:
    static void forward(const char* topic, const char* data, int len);
    static void forward_conn(bool connected);

    MessageHandler handler_ = nullptr;
    ConnectionHandler conn_handler_ = nullptr;
};

} // namespace transport
