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

}  // namespace nervous_system

namespace {

const nlohmann::json* resolve_json_path(const nlohmann::json& root, const std::string& path) {
    const nlohmann::json* current = &root;
    size_t start = 0;

    while (start < path.size()) {
        size_t dot = path.find('.', start);
        std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);

        if (!current->is_object() || !current->contains(key)) {
            return nullptr;
        }

        current = &(*current)[key];
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }

    return current;
}

bool is_operator_object(const nlohmann::json& json_value) {
    if (!json_value.is_object()) {
        return false;
    }
    for (auto it = json_value.begin(); it != json_value.end(); ++it) {
        if (!it.key().empty() && it.key()[0] == '$') {
            return true;
        }
    }
    return false;
}

bool compare_json_operator(const nlohmann::json& actual, const std::string& op, const nlohmann::json& expected) {
    if (op == "$exists") {
        if (!expected.is_boolean()) {
            return false;
        }
        return expected.get<bool>() ? !actual.is_null() : actual.is_null();
    }

    if (op == "$eq") {
        return actual == expected;
    }
    if (op == "$ne") {
        return actual != expected;
    }

    if (op == "$in") {
        if (!expected.is_array()) {
            return false;
        }
        for (const auto& candidate : expected) {
            if (candidate == actual) {
                return true;
            }
        }
        return false;
    }

    if (actual.is_number() && expected.is_number()) {
        double actual_value = actual.get<double>();
        double expected_value = expected.get<double>();
        if (op == "$gt") {
            return actual_value > expected_value;
        }
        if (op == "$gte") {
            return actual_value >= expected_value;
        }
        if (op == "$lt") {
            return actual_value < expected_value;
        }
        if (op == "$lte") {
            return actual_value <= expected_value;
        }
    }

    if (actual.is_string() && expected.is_string()) {
        const std::string actual_str = actual.get<std::string>();
        const std::string expected_str = expected.get<std::string>();
        if (op == "$gt") {
            return actual_str > expected_str;
        }
        if (op == "$gte") {
            return actual_str >= expected_str;
        }
        if (op == "$lt") {
            return actual_str < expected_str;
        }
        if (op == "$lte") {
            return actual_str <= expected_str;
        }
    }

    return false;
}

bool compare_condition_value(const nlohmann::json& actual, const nlohmann::json& expected);

bool object_condition_matches(const nlohmann::json& actual, const nlohmann::json& expected) {
    if (!expected.is_object()) {
        return actual == expected;
    }

    if (is_operator_object(expected)) {
        for (auto it = expected.begin(); it != expected.end(); ++it) {
            if (!compare_json_operator(actual, it.key(), it.value())) {
                return false;
            }
        }
        return true;
    }

    if (!actual.is_object()) {
        return false;
    }

    for (auto it = expected.begin(); it != expected.end(); ++it) {
        const std::string& key = it.key();
        const nlohmann::json& expected_value = it.value();

        if (!actual.contains(key)) {
            return false;
        }

        if (!compare_condition_value(actual[key], expected_value)) {
            return false;
        }
    }

    return true;
}

bool compare_condition_value(const nlohmann::json& actual, const nlohmann::json& expected) {
    if (expected.is_object()) {
        return object_condition_matches(actual, expected);
    }

    return actual == expected;
}

}  // anonymous namespace

namespace nervous_system {

bool ProcessingEngineImpl::matches_rule(const nlohmann::json& event, const Rule& rule) const {
    if (!rule.enabled) {
        return false;
    }

    if (event.contains("event_type")) {
        const auto& event_type = event["event_type"];
        if (!event_type.is_string() || event_type.get<std::string>() != rule.event_type) {
            return false;
        }
    } else {
        return false;
    }

    if (!rule.condition.is_null() && !rule.condition.empty()) {
        if (!condition_matches(event, rule.condition)) {
            return false;
        }
    }

    return true;
}

bool ProcessingEngineImpl::condition_matches(const nlohmann::json& event,
                                           const nlohmann::json& condition) const {
    if (condition.is_null() || (condition.is_object() && condition.empty())) {
        return true;
    }

    if (condition.contains("$all") && condition["$all"].is_array()) {
        for (const auto& sub_condition : condition["$all"]) {
            if (!condition_matches(event, sub_condition)) {
                return false;
            }
        }
        return true;
    }

    if (condition.contains("$any") && condition["$any"].is_array()) {
        for (const auto& sub_condition : condition["$any"]) {
            if (condition_matches(event, sub_condition)) {
                return true;
            }
        }
        return false;
    }

    if (condition.contains("$not")) {
        return !condition_matches(event, condition["$not"]);
    }

    for (auto it = condition.begin(); it != condition.end(); ++it) {
        const std::string& key = it.key();
        const nlohmann::json& expected_value = it.value();

        const nlohmann::json* actual_value = resolve_json_path(event, key);
        if (!actual_value) {
            actual_value = resolve_json_path(event, "payload." + key);
        }
        if (!actual_value) {
            return false;
        }

        if (!compare_condition_value(*actual_value, expected_value)) {
            return false;
        }
    }

    return true;
}

}  // namespace nervous_system
