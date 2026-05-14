#include "rabbitmq_publisher.hpp"

namespace nervous_system {

void MockRabbitMQPublisher::publish(const std::string& queue, const nlohmann::json& message) {
    published_messages_.emplace_back(queue, message);
}

}  // namespace nervous_system
