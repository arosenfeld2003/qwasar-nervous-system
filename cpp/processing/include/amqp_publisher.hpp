#pragma once

#include "processing_engine_impl.hpp"
#include <string>

namespace nervous_system {

// Real RabbitMQ publisher using rabbitmq-c (synchronous, single-channel).
// One connection per instance; not thread-safe — use one publisher per thread.
class AmqpRabbitMQPublisher : public RabbitMQPublisher {
public:
    explicit AmqpRabbitMQPublisher(const std::string& amqp_uri);
    ~AmqpRabbitMQPublisher() override;

    void publish(const std::string& queue, const nlohmann::json& message) override;

private:
    struct Impl;
    Impl* impl_;

    void connect(const std::string& uri);
    void declare_queue(const std::string& queue);
};

}  // namespace nervous_system
