#pragma once

#include "processing_engine_impl.hpp"
#include <memory>

namespace nervous_system {

// Simple mock publisher for testing/basic usage
class MockRabbitMQPublisher : public RabbitMQPublisher {
public:
    void publish(const std::string& queue, const nlohmann::json& message) override;
    
    const std::vector<std::pair<std::string, nlohmann::json>>& get_published() const {
        return published_messages_;
    }
    
    void clear() {
        published_messages_.clear();
    }

private:
    std::vector<std::pair<std::string, nlohmann::json>> published_messages_;
};

}  // namespace nervous_system
