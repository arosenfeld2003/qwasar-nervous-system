#include "processing_engine_impl.hpp"
#include <chrono>

namespace nervous_system {

ProcessingEngineImpl::ProcessingEngineImpl(std::shared_ptr<RabbitMQPublisher> publisher)
    : publisher_(publisher) {}

void ProcessingEngineImpl::set_publisher(std::shared_ptr<RabbitMQPublisher> publisher) {
    publisher_ = publisher;
}

void ProcessingEngineImpl::load_rules(const std::vector<Rule>& rules) {
    std::lock_guard<std::mutex> lock(rules_mutex_);
    rules_ = rules;
}

void ProcessingEngineImpl::evaluate(const nlohmann::json& event, AlarmCallback on_alarm) {
    std::lock_guard<std::mutex> lock(rules_mutex_);

    for (const auto& rule : rules_) {
        if (matches_rule(event, rule)) {
            // Create alarm decision from template
            AlarmDecision alarm = rule.alarm_template;
            
            // Set device_id from event
            if (event.contains("device_id")) {
                alarm.device_id = event["device_id"].get<std::string>();
            }
            
            // Set triggered_at_ms from event or current time
            if (event.contains("timestamp_ms")) {
                alarm.triggered_at_ms = event["timestamp_ms"].get<uint64_t>();
            } else {
                auto now = std::chrono::system_clock::now();
                auto duration = now.time_since_epoch();
                alarm.triggered_at_ms = 
                    std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
            }

            // Call the callback if provided
            if (on_alarm) {
                on_alarm(alarm);
            }

            // Publish to RabbitMQ if publisher is available
            if (publisher_) {
                publisher_->publish("alarms.dispatch", alarm.to_json());
            }
        }
    }
}

bool ProcessingEngineImpl::matches_rule(const nlohmann::json& event, const Rule& rule) const {
    // Check event_type match
    if (event.contains("event_type")) {
        const auto& event_type = event["event_type"];
        if (event_type.is_string() && event_type.get<std::string>() != rule.event_type) {
            return false;
        }
    } else {
        return false;
    }

    // Check condition match (if present)
    if (!rule.condition_json.empty()) {
        try {
            auto condition = nlohmann::json::parse(rule.condition_json);
            if (!condition_matches(event, condition)) {
                return false;
            }
        } catch (const nlohmann::json::exception& e) {
            // Invalid condition JSON, skip this rule
            return false;
        }
    }

    return true;
}

bool ProcessingEngineImpl::condition_matches(const nlohmann::json& event,
                                           const nlohmann::json& condition) const {
    // Check if all condition keys exist in event and match
    for (auto it = condition.begin(); it != condition.end(); ++it) {
        const std::string& key = it.key();
        const nlohmann::json& expected_value = it.value();

        // For nested objects like "payload", check if event has this key
        if (key == "payload" && event.contains("payload")) {
            if (!condition_matches(event["payload"], expected_value)) {
                return false;
            }
        } else if (event.contains(key)) {
            // Direct key-value comparison
            if (event[key] != expected_value) {
                return false;
            }
        } else {
            // Key not found in event
            return false;
        }
    }

    return true;
}

}  // namespace nervous_system
