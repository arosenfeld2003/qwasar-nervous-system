#pragma once

#include "processing_engine.hpp"
#include <map>
#include <mutex>
#include <memory>
#include <nlohmann/json.hpp>

namespace nervous_system {

class RabbitMQPublisher {
public:
    virtual ~RabbitMQPublisher() = default;
    virtual void publish(const std::string& queue, const nlohmann::json& message) = 0;
};

class ProcessingEngineImpl : public IProcessingEngine {
public:
    explicit ProcessingEngineImpl(std::shared_ptr<RabbitMQPublisher> publisher = nullptr);
    ~ProcessingEngineImpl() override = default;

    void load_rules(const std::vector<Rule>& rules) override;
    void evaluate(const nlohmann::json& event, AlarmCallback on_alarm) override;

    // Configuration methods
    void set_publisher(std::shared_ptr<RabbitMQPublisher> publisher);

private:
    std::vector<Rule> rules_;
    std::shared_ptr<RabbitMQPublisher> publisher_;
    mutable std::mutex rules_mutex_;

    bool matches_rule(const nlohmann::json& event, const Rule& rule) const;
    bool condition_matches(const nlohmann::json& event, const nlohmann::json& condition) const;
};

}  // namespace nervous_system
