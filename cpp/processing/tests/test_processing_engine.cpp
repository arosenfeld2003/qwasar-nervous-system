#include "processing_engine_impl.hpp"
#include "rabbitmq_publisher.hpp"
#include <cassert>
#include <iostream>

using namespace nervous_system;
using json = nlohmann::json;

// Test counters
int tests_passed = 0;
int tests_failed = 0;

void assert_true(bool condition, const std::string& test_name) {
    if (condition) {
        std::cout << "✓ " << test_name << std::endl;
        tests_passed++;
    } else {
        std::cerr << "✗ " << test_name << std::endl;
        tests_failed++;
    }
}

void assert_equal(const std::string& actual, const std::string& expected, 
                  const std::string& test_name) {
    assert_true(actual == expected, test_name);
}

void assert_equal(size_t actual, size_t expected, const std::string& test_name) {
    assert_true(actual == expected, test_name);
}

// ============================================================================
// Test Suite
// ============================================================================

void test_load_rules() {
    std::cout << "\n=== Test: Load Rules ===" << std::endl;
    
    ProcessingEngineImpl engine;
    
    // Create some test rules
    AlarmDecision alarm_template;
    alarm_template.alarm_type = "motion";
    alarm_template.severity = "high";
    alarm_template.message = "Motion detected";
    
    Rule rule;
    rule.id = "rule-001";
    rule.event_type = "motion_detected";
    rule.condition_json = R"({"zone": "front-door"})";
    rule.alarm_template = alarm_template;
    
    std::vector<Rule> rules = {rule};
    engine.load_rules(rules);
    
    // We can't directly check the internal state, but we verify it doesn't crash
    assert_true(true, "Load rules without error");
}

void test_match_simple_event_type() {
    std::cout << "\n=== Test: Match Simple Event Type ===" << std::endl;
    
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // Setup rule
    AlarmDecision alarm_template;
    alarm_template.alarm_type = "motion";
    alarm_template.severity = "high";
    alarm_template.message = "Motion detected";
    
    Rule rule;
    rule.id = "rule-001";
    rule.event_type = "motion_detected";
    rule.condition_json = "";
    rule.alarm_template = alarm_template;
    
    engine.load_rules({rule});
    
    // Create matching event
    json event = {
        {"device_id", "device-123"},
        {"event_type", "motion_detected"},
        {"timestamp_ms", 1234567890UL}
    };
    
    int alarm_count = 0;
    auto callback = [&alarm_count](const AlarmDecision& alarm) {
        alarm_count++;
    };
    
    engine.evaluate(event, callback);
    
    assert_equal(alarm_count, 1, "Matching event triggers callback");
    assert_equal(publisher->get_published().size(), size_t(1), 
                "Alarm published to RabbitMQ");
    assert_equal(publisher->get_published()[0].first, std::string("alarms.dispatch"),
                "Published to correct queue");
}

void test_no_match_wrong_event_type() {
    std::cout << "\n=== Test: No Match - Wrong Event Type ===" << std::endl;
    
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // Setup rule
    AlarmDecision alarm_template;
    alarm_template.alarm_type = "motion";
    alarm_template.severity = "high";
    alarm_template.message = "Motion detected";
    
    Rule rule;
    rule.id = "rule-001";
    rule.event_type = "motion_detected";
    rule.condition_json = "";
    rule.alarm_template = alarm_template;
    
    engine.load_rules({rule});
    
    // Create non-matching event (different event_type)
    json event = {
        {"device_id", "device-123"},
        {"event_type", "temperature_reading"},
        {"timestamp_ms", 1234567890UL}
    };
    
    int alarm_count = 0;
    auto callback = [&alarm_count](const AlarmDecision& alarm) {
        alarm_count++;
    };
    
    engine.evaluate(event, callback);
    
    assert_equal(alarm_count, 0, "Non-matching event does not trigger callback");
    assert_equal(publisher->get_published().size(), size_t(0),
                "No alarm published for non-matching event");
}

void test_match_with_condition() {
    std::cout << "\n=== Test: Match with Condition ===" << std::endl;
    
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // Setup rule with condition
    AlarmDecision alarm_template;
    alarm_template.alarm_type = "motion";
    alarm_template.severity = "high";
    alarm_template.message = "Motion at front door";
    
    Rule rule;
    rule.id = "rule-001";
    rule.event_type = "motion_detected";
    rule.condition_json = R"({"zone": "front-door"})";
    rule.alarm_template = alarm_template;
    
    engine.load_rules({rule});
    
    // Create matching event with condition
    json event = {
        {"device_id", "device-123"},
        {"event_type", "motion_detected"},
        {"zone", "front-door"},
        {"timestamp_ms", 1234567890UL}
    };
    
    int alarm_count = 0;
    auto callback = [&alarm_count](const AlarmDecision& alarm) {
        alarm_count++;
    };
    
    engine.evaluate(event, callback);
    
    assert_equal(alarm_count, 1, "Event matching condition triggers callback");
}

void test_no_match_condition_mismatch() {
    std::cout << "\n=== Test: No Match - Condition Mismatch ===" << std::endl;
    
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // Setup rule with condition
    AlarmDecision alarm_template;
    alarm_template.alarm_type = "motion";
    alarm_template.severity = "high";
    alarm_template.message = "Motion at front door";
    
    Rule rule;
    rule.id = "rule-001";
    rule.event_type = "motion_detected";
    rule.condition_json = R"({"zone": "front-door"})";
    rule.alarm_template = alarm_template;
    
    engine.load_rules({rule});
    
    // Create event with wrong zone (condition mismatch)
    json event = {
        {"device_id", "device-123"},
        {"event_type", "motion_detected"},
        {"zone", "back-yard"},
        {"timestamp_ms", 1234567890UL}
    };
    
    int alarm_count = 0;
    auto callback = [&alarm_count](const AlarmDecision& alarm) {
        alarm_count++;
    };
    
    engine.evaluate(event, callback);
    
    assert_equal(alarm_count, 0, "Event not matching condition does not trigger callback");
}

void test_multiple_rules_multiple_matches() {
    std::cout << "\n=== Test: Multiple Rules with Multiple Matches ===" << std::endl;
    
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // Setup multiple rules
    AlarmDecision alarm_template_1;
    alarm_template_1.alarm_type = "motion";
    alarm_template_1.severity = "high";
    alarm_template_1.message = "Motion detected";
    
    Rule rule1;
    rule1.id = "rule-001";
    rule1.event_type = "motion_detected";
    rule1.condition_json = "";
    rule1.alarm_template = alarm_template_1;
    
    AlarmDecision alarm_template_2;
    alarm_template_2.alarm_type = "security";
    alarm_template_2.severity = "high";
    alarm_template_2.message = "Any motion";
    
    Rule rule2;
    rule2.id = "rule-002";
    rule2.event_type = "motion_detected";
    rule2.condition_json = "";
    rule2.alarm_template = alarm_template_2;
    
    engine.load_rules({rule1, rule2});
    
    // Create event that matches both rules
    json event = {
        {"device_id", "device-123"},
        {"event_type", "motion_detected"},
        {"timestamp_ms", 1234567890UL}
    };
    
    int alarm_count = 0;
    auto callback = [&alarm_count](const AlarmDecision& alarm) {
        alarm_count++;
    };
    
    engine.evaluate(event, callback);
    
    assert_equal(alarm_count, 2, "Event matching multiple rules triggers callback twice");
    assert_equal(publisher->get_published().size(), size_t(2),
                "Multiple alarms published to RabbitMQ");
}

void test_alarm_decision_contains_device_id() {
    std::cout << "\n=== Test: AlarmDecision Contains Device ID ===" << std::endl;
    
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // Setup rule
    AlarmDecision alarm_template;
    alarm_template.alarm_type = "motion";
    alarm_template.severity = "high";
    alarm_template.message = "Motion detected";
    
    Rule rule;
    rule.id = "rule-001";
    rule.event_type = "motion_detected";
    rule.condition_json = "";
    rule.alarm_template = alarm_template;
    
    engine.load_rules({rule});
    
    // Create event with specific device_id
    json event = {
        {"device_id", "device-xyz-789"},
        {"event_type", "motion_detected"},
        {"timestamp_ms", 1234567890UL}
    };
    
    engine.evaluate(event, nullptr);
    
    assert_true(publisher->get_published().size() > 0, "Alarm was published");
    if (publisher->get_published().size() > 0) {
        auto published_alarm = publisher->get_published()[0].second;
        assert_equal(published_alarm["device_id"].get<std::string>(), 
                    std::string("device-xyz-789"),
                    "Published alarm contains correct device_id");
    }
}

void test_alarm_decision_contains_timestamp() {
    std::cout << "\n=== Test: AlarmDecision Contains Timestamp ===" << std::endl;
    
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // Setup rule
    AlarmDecision alarm_template;
    alarm_template.alarm_type = "motion";
    alarm_template.severity = "high";
    alarm_template.message = "Motion detected";
    
    Rule rule;
    rule.id = "rule-001";
    rule.event_type = "motion_detected";
    rule.condition_json = "";
    rule.alarm_template = alarm_template;
    
    engine.load_rules({rule});
    
    // Create event with timestamp
    uint64_t test_timestamp = 1234567890UL;
    json event = {
        {"device_id", "device-123"},
        {"event_type", "motion_detected"},
        {"timestamp_ms", test_timestamp}
    };
    
    engine.evaluate(event, nullptr);
    
    assert_true(publisher->get_published().size() > 0, "Alarm was published");
    if (publisher->get_published().size() > 0) {
        auto published_alarm = publisher->get_published()[0].second;
        assert_equal(published_alarm["triggered_at_ms"].get<uint64_t>(),
                    test_timestamp,
                    "Published alarm contains correct timestamp");
    }
}

void test_event_without_timestamp_gets_current_time() {
    std::cout << "\n=== Test: Event Without Timestamp Gets Current Time ===" << std::endl;
    
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // Setup rule
    AlarmDecision alarm_template;
    alarm_template.alarm_type = "motion";
    alarm_template.severity = "high";
    alarm_template.message = "Motion detected";
    
    Rule rule;
    rule.id = "rule-001";
    rule.event_type = "motion_detected";
    rule.condition_json = "";
    rule.alarm_template = alarm_template;
    
    engine.load_rules({rule});
    
    // Create event without timestamp
    json event = {
        {"device_id", "device-123"},
        {"event_type", "motion_detected"}
    };
    
    auto before = std::chrono::system_clock::now();
    auto duration_before = before.time_since_epoch();
    auto ms_before = std::chrono::duration_cast<std::chrono::milliseconds>(duration_before).count();
    
    engine.evaluate(event, nullptr);
    
    auto after = std::chrono::system_clock::now();
    auto duration_after = after.time_since_epoch();
    auto ms_after = std::chrono::duration_cast<std::chrono::milliseconds>(duration_after).count();
    
    assert_true(publisher->get_published().size() > 0, "Alarm was published");
    if (publisher->get_published().size() > 0) {
        auto published_alarm = publisher->get_published()[0].second;
        auto alarm_time = published_alarm["triggered_at_ms"].get<uint64_t>();
        bool in_range = alarm_time >= static_cast<uint64_t>(ms_before) && 
                       alarm_time <= static_cast<uint64_t>(ms_after);
        assert_true(in_range, "Published alarm has reasonable current timestamp");
    }
}

void test_callback_receives_all_alarm_details() {
    std::cout << "\n=== Test: Callback Receives All Alarm Details ===" << std::endl;
    
    ProcessingEngineImpl engine;
    
    // Setup rule
    AlarmDecision alarm_template;
    alarm_template.alarm_type = "motion";
    alarm_template.severity = "high";
    alarm_template.message = "Motion at front door";
    
    Rule rule;
    rule.id = "rule-001";
    rule.event_type = "motion_detected";
    rule.condition_json = "";
    rule.alarm_template = alarm_template;
    
    engine.load_rules({rule});
    
    // Create event
    json event = {
        {"device_id", "device-456"},
        {"event_type", "motion_detected"},
        {"timestamp_ms", 9876543210UL}
    };
    
    AlarmDecision received_alarm;
    auto callback = [&received_alarm](const AlarmDecision& alarm) {
        received_alarm = alarm;
    };
    
    engine.evaluate(event, callback);
    
    assert_equal(received_alarm.device_id, std::string("device-456"), 
                "Callback receives device_id");
    assert_equal(received_alarm.alarm_type, std::string("motion"),
                "Callback receives alarm_type");
    assert_equal(received_alarm.severity, std::string("high"),
                "Callback receives severity");
    assert_equal(received_alarm.message, std::string("Motion at front door"),
                "Callback receives message");
    assert_equal(received_alarm.triggered_at_ms, uint64_t(9876543210UL),
                "Callback receives timestamp");
}

void test_empty_rules_no_alarms() {
    std::cout << "\n=== Test: Empty Rules - No Alarms ===" << std::endl;
    
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // Load empty rules
    engine.load_rules({});
    
    // Create event
    json event = {
        {"device_id", "device-123"},
        {"event_type", "motion_detected"},
        {"timestamp_ms", 1234567890UL}
    };
    
    int alarm_count = 0;
    auto callback = [&alarm_count](const AlarmDecision& alarm) {
        alarm_count++;
    };
    
    engine.evaluate(event, callback);
    
    assert_equal(alarm_count, 0, "No alarms triggered with empty rules");
}

int main() {
    std::cout << "\n╔════════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║        Processing Engine Unit Tests                         ║" << std::endl;
    std::cout << "╚════════════════════════════════════════════════════════════╝" << std::endl;
    
    // Run all tests
    test_load_rules();
    test_match_simple_event_type();
    test_no_match_wrong_event_type();
    test_match_with_condition();
    test_no_match_condition_mismatch();
    test_multiple_rules_multiple_matches();
    test_alarm_decision_contains_device_id();
    test_alarm_decision_contains_timestamp();
    test_event_without_timestamp_gets_current_time();
    test_callback_receives_all_alarm_details();
    test_empty_rules_no_alarms();
    
    // Print summary
    std::cout << "\n" << std::string(64, '=') << std::endl;
    std::cout << "Test Results: " << tests_passed << " passed, " << tests_failed << " failed" 
              << std::endl;
    std::cout << std::string(64, '=') << "\n" << std::endl;
    
    return tests_failed == 0 ? 0 : 1;
}
