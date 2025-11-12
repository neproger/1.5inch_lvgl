#include "infra/transport/mqtt_transport.hpp"
#include "infra/transport/ha_mqtt.hpp"

namespace transport {

namespace {
MqttTransport* s_active = nullptr;
}

void MqttTransport::set_handler(MessageHandler handler)
{
    handler_ = handler;
}

void MqttTransport::set_connection_handler(ConnectionHandler handler)
{
    conn_handler_ = handler;
    ha_mqtt::set_connection_handler(&MqttTransport::forward_conn);
}

esp_err_t MqttTransport::start()
{
    s_active = this;
    ha_mqtt::set_message_handler(&MqttTransport::forward);
    return ha_mqtt::start();
}

esp_err_t MqttTransport::publish(const char* topic, const char* payload, int qos, bool retain)
{
    return ha_mqtt::publish(topic, payload, qos, retain);
}

esp_err_t MqttTransport::subscribe(const char* topic, int qos)
{
    return ha_mqtt::subscribe(topic, qos);
}

bool MqttTransport::is_connected() const
{
    return ha_mqtt::is_connected();
}

void MqttTransport::forward(const char* topic, const char* data, int len)
{
    if (s_active && s_active->handler_) {
        s_active->handler_(topic, data, len);
    }
}

void MqttTransport::forward_conn(bool connected)
{
    if (s_active && s_active->conn_handler_) {
        s_active->conn_handler_(connected);
    }
}

} // namespace transport
