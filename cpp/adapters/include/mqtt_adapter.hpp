#pragma once

#include "adapter.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace mqtt {
class async_client;
}

namespace nervous_system {

class RabbitMQClient;

class MqttAdapter : public IAdapter {
public:
    MqttAdapter(std::string broker_url, std::string topic);
    ~MqttAdapter() override;

    void start(EventCallback on_event) override;
    void stop() override;
    std::string protocol_name() const override { return "mqtt"; }

private:
    std::string broker_url_;
    std::string topic_;
    std::string client_id_;
    std::atomic<bool> running_ = false;

    std::unique_ptr<mqtt::async_client> client_;
    std::unique_ptr<RabbitMQClient> rabbitmq_client_;

    class Callback;
    std::unique_ptr<Callback> callback_;

    EventCallback on_event_;
    std::mutex callback_mutex_;

    void handle_mqtt_message(const std::string& topic, const std::string& payload);
    void publish_device_event(const DeviceEvent& event);
};

}  // namespace nervous_system
