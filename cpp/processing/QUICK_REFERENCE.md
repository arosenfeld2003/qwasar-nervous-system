# Quick Reference — Processing Engine API

## Class: ProcessingEngineImpl

**Header**: `processing_engine_impl.hpp`

```cpp
class ProcessingEngineImpl : public IProcessingEngine {
public:
    // Constructor with optional publisher
    explicit ProcessingEngineImpl(std::shared_ptr<RabbitMQPublisher> publisher = nullptr);
    
    // Load rules (thread-safe)
    void load_rules(const std::vector<Rule>& rules) override;
    
    // Evaluate event and trigger callbacks/publishing
    void evaluate(const nlohmann::json& event, AlarmCallback on_alarm) override;
    
    // Update publisher at runtime
    void set_publisher(std::shared_ptr<RabbitMQPublisher> publisher);
};
```

### Example
```cpp
auto publisher = std::make_shared<MockRabbitMQPublisher>();
ProcessingEngineImpl engine(publisher);

// Load rules
auto rules = RulesLoader::load_from_json_file("config/rules.json");
engine.load_rules(rules);

// Evaluate event
nlohmann::json event = {
    {"device_id", "device-123"},
    {"event_type", "motion_detected"},
    {"zone", "front-door"},
    {"timestamp_ms", 1234567890UL}
};

engine.evaluate(event, [](const AlarmDecision& alarm) {
    std::cout << alarm.message << std::endl;
});
```

---

## Class: RulesLoader

**Header**: `rules_loader.hpp`

```cpp
class RulesLoader {
public:
    // Load rules from JSON file
    static std::vector<Rule> load_from_json_file(const std::string& filepath);
    
    // Load rules from JSON string
    static std::vector<Rule> load_from_json_string(const std::string& json_string);
};
```

### Example
```cpp
// From file
auto rules = RulesLoader::load_from_json_file("config/rules.json");

// From JSON string
std::string json_str = R"({"rules": [...]})";
auto rules = RulesLoader::load_from_json_string(json_str);
```

---

## Class: MockRabbitMQPublisher

**Header**: `rabbitmq_publisher.hpp`

```cpp
class MockRabbitMQPublisher : public RabbitMQPublisher {
public:
    void publish(const std::string& queue, const nlohmann::json& message) override;
    
    // For testing: retrieve published messages
    const std::vector<std::pair<std::string, nlohmann::json>>& get_published() const;
    
    void clear();
};
```

### Example
```cpp
auto publisher = std::make_shared<MockRabbitMQPublisher>();
engine.set_publisher(publisher);

engine.evaluate(event, nullptr);

// Check what was published
auto& messages = publisher->get_published();
assert(messages.size() == 1);
assert(messages[0].first == "alarms.dispatch");
```

---

## Struct: Rule

**Header**: `processing_engine.hpp`

```cpp
struct Rule {
    std::string id;                    // Unique rule identifier
    std::string event_type;            // Event type to match
    nlohmann::json condition;          // Structured JSON condition object (empty = any)
    AlarmDecision alarm_template;      // Template for alarm when matched
};
```

---

## Struct: AlarmDecision

**Header**: `processing_engine.hpp`

```cpp
struct AlarmDecision {
    std::string device_id;             // Originating device
    std::string alarm_type;            // Alarm category
    std::string severity;              // Severity level
    std::string message;               // Human-readable message
    uint64_t triggered_at_ms;          // Timestamp in milliseconds
    
    nlohmann::json to_json() const;   // Convert to JSON
};
```

---

## Callback Type

**Header**: `processing_engine.hpp`

```cpp
using AlarmCallback = std::function<void(AlarmDecision)>;
```

---

## Complete Example

```cpp
#include "processing_engine_impl.hpp"
#include "rabbitmq_publisher.hpp"
#include "rules_loader.hpp"
#include <iostream>

using namespace nervous_system;
using json = nlohmann::json;

int main() {
    // 1. Create publisher and engine
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    
    // 2. Load rules from JSON
    try {
        auto rules = RulesLoader::load_from_json_file("config/rules.json");
        engine.load_rules(rules);
        std::cout << "Loaded " << rules.size() << " rules" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load rules: " << e.what() << std::endl;
        return 1;
    }
    
    // 3. Create sample events and evaluate
    std::vector<json> events = {
        {
            {"device_id", "front-door-cam"},
            {"event_type", "motion_detected"},
            {"zone", "front-door"},
            {"timestamp_ms", 1234567890UL}
        },
        {
            {"device_id", "garage-sensor"},
            {"event_type", "temperature_reading"},
            {"payload", {{"celsius", 45}}},
            {"timestamp_ms", 1234567895UL}
        }
    };
    
    // 4. Process events
    for (const auto& event : events) {
        engine.evaluate(event, [](const AlarmDecision& alarm) {
            std::cout << "🚨 ALARM: [" << alarm.severity << "] " 
                     << alarm.message << std::endl;
        });
    }
    
    // 5. Check what was published (for testing)
    std::cout << "\nPublished " << publisher->get_published().size() 
              << " messages to RabbitMQ" << std::endl;
    
    return 0;
}
```

---

## Configuration Example

**config/rules.json**:
```json
{
  "rules": [
    {
      "id": "rule-001",
      "event_type": "motion_detected",
      "condition": { "zone": "front-door" },
      "alarm": {
        "alarm_type": "motion",
        "severity": "high",
        "message": "Motion at front door"
      }
    },
    {
      "id": "rule-002",
      "event_type": "temperature_reading",
      "condition": {},
      "alarm": {
        "alarm_type": "thermal",
        "severity": "medium",
        "message": "Temperature alert"
      }
    }
  ]
}
```

---

## Namespace

All classes are in: `namespace nervous_system`

Use with:
```cpp
using namespace nervous_system;
// or
nervous_system::ProcessingEngineImpl engine;
```

---

## Error Handling

### RulesLoader Exceptions

```cpp
try {
    auto rules = RulesLoader::load_from_json_file("invalid.json");
} catch (const std::exception& e) {
    // Possible errors:
    // - "Could not open rules file: ..."
    // - "Invalid rules JSON: 'rules' array not found"
    // - "Invalid rule: missing required fields"
    std::cerr << e.what() << std::endl;
}
```

---

## Thread Safety

- ✅ Multiple threads can call `evaluate()` simultaneously
- ✅ `load_rules()` and `evaluate()` are protected by mutex
- ✅ Safe to update rules while events are being processed
- ⚠️ Publisher must be thread-safe if used from multiple threads

---

## Compilation

### Minimal Example
```bash
# Source files
g++ -std=c++20 -I. \
    cpp/processing/src/processing_engine.cpp \
    cpp/processing/src/rabbitmq_publisher.cpp \
    your_code.cpp \
    -o app
```

### With CMake
```bash
cmake -S cpp -B cpp/build
cmake --build cpp/build
```

---

## Testing

```cpp
// Run all 11 tests
./cpp/build/test_processing_engine

// Expected output:
// ✓ Load rules without error
// ✓ Matching event triggers callback
// ✓ Alarm published to RabbitMQ
// ... (8 more tests)
// Test Results: 11 passed, 0 failed
```
