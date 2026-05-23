#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace nervous_system {

struct AlarmDecision {
    std::string device_id;
    std::string alarm_type;
    std::string severity;
    std::string message;
    uint64_t triggered_at_ms;

    nlohmann::json to_json() const {
        return {
            {"device_id", device_id},
            {"alarm_type", alarm_type},
            {"severity", severity},
            {"message", message},
            {"triggered_at_ms", triggered_at_ms},
        };
    }
};

struct Rule {
    std::string id;
    std::string description;
    std::string event_type;
    nlohmann::json condition;
    AlarmDecision alarm_template;
    int priority = 0;
    bool enabled = true;
};

using AlarmCallback = std::function<void(AlarmDecision)>;

class IProcessingEngine {
public:
    virtual ~IProcessingEngine() = default;

    virtual void load_rules(const std::vector<Rule>& rules) = 0;
    virtual void evaluate(const nlohmann::json& event, AlarmCallback on_alarm) = 0;
};

}  // namespace nervous_system
