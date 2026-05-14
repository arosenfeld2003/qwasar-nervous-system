# Processing Engine — C++ Implementation

The processing engine evaluates incoming `DeviceEvents` against a set of rules and emits `AlarmDecision` objects to RabbitMQ.

## Architecture

### Components

- **`IProcessingEngine`** — Abstract interface for rule-based event processing
- **`ProcessingEngineImpl`** — Concrete implementation using in-memory rule storage
- **`RabbitMQPublisher`** — Abstract interface for publishing alarms (with `MockRabbitMQPublisher` for testing)
- **`RulesLoader`** — JSON rules file parser and loader

### Data Structures

#### DeviceEvent (input)
```json
{
  "device_id": "device-123",
  "event_type": "motion_detected",
  "timestamp_ms": 1234567890,
  "zone": "front-door"
}
```

#### Rule (configuration)
```json
{
  "id": "rule-001",
  "event_type": "motion_detected",
  "condition": { "zone": "front-door" },
  "alarm": {
    "alarm_type": "motion",
    "severity": "high",
    "message": "Motion at front door"
  }
}
```

#### AlarmDecision (output)
```json
{
  "device_id": "device-123",
  "alarm_type": "motion",
  "severity": "high",
  "message": "Motion at front door",
  "triggered_at_ms": 1234567890
}
```

## Building

### Prerequisites

- C++20 compiler (g++, clang, or MSVC)
- CMake 3.20+
- nlohmann/json (fetched automatically by CMake)

### Build Steps

```bash
# From the project root
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug
cmake --build cpp/build --parallel

# Run tests
./cpp/build/test_processing_engine
```

### Running Tests

```bash
# After building
cd cpp/build
./test_processing_engine
```

Expected output:
```
════════════════════════════════════════════════════════════
        Processing Engine Unit Tests
════════════════════════════════════════════════════════════

=== Test: Load Rules ===
✓ Load rules without error

=== Test: Match Simple Event Type ===
✓ Matching event triggers callback
✓ Alarm published to RabbitMQ
✓ Published to correct queue

[... more tests ...]

════════════════════════════════════════════════════════════
Test Results: 11 passed, 0 failed
════════════════════════════════════════════════════════════
```

## Usage Example

### Loading Rules from JSON

```cpp
#include "rules_loader.hpp"
#include "processing_engine_impl.hpp"
#include <iostream>

using namespace nervous_system;

int main() {
    // Load rules from file
    auto rules = RulesLoader::load_from_json_file("config/rules.json");
    
    // Create engine with mock publisher
    auto publisher = std::make_shared<MockRabbitMQPublisher>();
    ProcessingEngineImpl engine(publisher);
    engine.load_rules(rules);
    
    // Process an event
    nlohmann::json event = {
        {"device_id", "device-456"},
        {"event_type", "motion_detected"},
        {"zone", "front-door"},
        {"timestamp_ms", 1234567890UL}
    };
    
    // Define callback for alarms
    auto on_alarm = [](const AlarmDecision& alarm) {
        std::cout << "🚨 Alarm triggered: " << alarm.message << std::endl;
    };
    
    // Evaluate event
    engine.evaluate(event, on_alarm);
    
    return 0;
}
```

## Rule Format

Rules are stored in `config/rules.json` with the following structure:

```json
{
  "rules": [
    {
      "id": "rule-001",
      "event_type": "motion_detected",
      "condition": {
        "zone": "front-door"
      },
      "alarm": {
        "alarm_type": "motion",
        "severity": "high",
        "message": "Motion at front door"
      }
    }
  ]
}
```

### Rule Fields

| Field | Type | Description | Required |
|-------|------|-------------|----------|
| `id` | string | Unique rule identifier | ✓ |
| `event_type` | string | Event type to match | ✓ |
| `condition` | object | Attributes to match in event | ✗ (defaults to any) |
| `alarm` | object | Template for alarm when rule matches | ✓ |

### Alarm Template Fields

| Field | Type | Description |
|-------|------|-------------|
| `alarm_type` | string | Category of alarm (e.g., "motion", "security") |
| `severity` | string | Severity level (e.g., "high", "medium", "low") |
| `message` | string | Human-readable description |

## Condition Matching

Conditions support:

- **Exact field matching** — Event attribute must equal condition value
  ```json
  { "zone": "front-door" }
  ```
- **Nested field matching** — Match within payload or nested objects
  ```json
  { "payload": { "temperature": 35 } }
  ```
- **Empty condition** — Matches any event of the given type
  ```json
  { }
  ```

## Files

```
cpp/processing/
├── include/
│   ├── processing_engine.hpp          # IProcessingEngine interface
│   ├── processing_engine_impl.hpp     # Implementation header
│   ├── rabbitmq_publisher.hpp         # RabbitMQ publisher interface
│   └── rules_loader.hpp               # JSON rules loader
├── src/
│   ├── processing_engine.cpp          # Implementation source
│   └── rabbitmq_publisher.cpp         # Publisher implementation
├── tests/
│   └── test_processing_engine.cpp     # Unit tests (11 test cases)
├── config/
│   └── rules.json                     # Sample rules configuration
├── CMakeLists.txt                     # Build configuration
└── README.md                          # This file
```

## Test Coverage

Unit tests include:

1. ✓ Load rules from Rule vector
2. ✓ Match simple event type
3. ✓ No match — wrong event type
4. ✓ Match with condition
5. ✓ No match — condition mismatch
6. ✓ Multiple rules with multiple matches
7. ✓ AlarmDecision contains device_id
8. ✓ AlarmDecision contains timestamp
9. ✓ Event without timestamp gets current time
10. ✓ Callback receives all alarm details
11. ✓ Empty rules — no alarms

All tests verify both the callback mechanism and RabbitMQ publishing behavior.

## Future Enhancements

- **Real RabbitMQ integration** — Replace `MockRabbitMQPublisher` with `amqp-cpp`-based implementation
- **Redis hot-reload** — Load rules from Redis instead of static file
- **Rule priority/ordering** — Execute rules in defined order with early exit
- **Complex conditions** — Support logical operators (AND, OR, NOT)
- **Rule versioning** — Track rule changes and apply versioned rules
- **Performance monitoring** — Measure rule evaluation latency
- **Rule validation** — Syntax/semantic validation at load time

## Dependencies

- **nlohmann/json** — JSON parsing and serialization (included via CMake FetchContent)
- **C++20 Standard Library** — For threading, chrono, functional

## Thread Safety

`ProcessingEngineImpl::evaluate()` is thread-safe with respect to rule updates:

- Rules are protected by `std::mutex` during `load_rules()` and `evaluate()`
- Multiple threads can evaluate events concurrently
- Rule updates are atomic from the evaluator's perspective
