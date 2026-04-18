# Language Split Strategy

## Team

| Developer | Language | Role |
|-----------|----------|------|
| Alex      | Rust     | Event Queue, Alarm Dispatcher, system orchestration |
| Tim       | C++      | Protocol Adapters (MQTT, Zigbee, Z-Wave, HTTP), Processing Engine |

## Why This Split

### C++ owns the Protocol Adapters and Processing Engine

The IoT ecosystem has deep C++ library coverage that would be painful to replicate:

- **MQTT**: `libmosquitto`, `Paho MQTT C++`
- **Zigbee**: `zigbee-shepherd` C bindings, `ZBOSS`
- **Z-Wave**: `OpenZWave` (C++ native)
- **HTTP**: `libcurl`, `Boost.Beast`, `cpp-httplib`

The Processing Engine's rules evaluation and ML model inference also have strong C++ support (`ONNX Runtime`, `TensorFlow Lite C API`, custom rule trees).

### Rust owns the Event Queue and Alarm Dispatcher

Rust's strengths align directly with these components:

- **Event Queue**: `tokio` async runtime + `crossbeam` channels give a safe, high-throughput FIFO with backpressure support. No data races by construction.
- **Alarm Dispatcher**: Rust's `reqwest` (HTTP), `lettre` (email), and `twilio` crates cover all alarm modalities. The dispatcher is latency-sensitive and benefits from Rust's predictable performance.
- **Orchestration / config**: Rust's `serde`/`serde_json` ecosystem makes config parsing and event schema validation ergonomic.

## Integration Points

```
C++ Adapter → [publishes JSON event] → Broker → [consumes] → Rust Queue → Rust Dispatcher
C++ Engine  ← [pulls event for rule eval] ← Rust Queue
C++ Engine  → [publishes alarm decision] → Rust Dispatcher
```

The shared contract is a JSON event schema (see `architecture.md`). Consider protobuf if JSON serialization becomes a bottleneck.

## Directory Layout (proposed)

```
eng_lab_4/
├── cpp/
│   ├── adapters/          # MQTT, Zigbee, Z-Wave, HTTP adapters
│   └── processing/        # Rules engine, ML inference
├── rust/
│   ├── queue/             # Unified event queue service
│   └── dispatcher/        # Alarm notification service
├── docs/
├── docker/                # Docker Compose for local broker + services
└── schema/                # Shared JSON/protobuf event schemas
```

## Build Tooling

| Language | Build System | Notes |
|----------|-------------|-------|
| Rust     | Cargo        | Standard; workspace for queue + dispatcher |
| C++      | CMake        | CMakePresets.json for consistent local + CI builds |

## Learning Goals per Developer

**Alex (Rust)**
- Async programming with `tokio`
- Safe concurrent data structures
- Working with external message brokers from Rust

**Tim (C++)**
- Modern C++17/20 patterns
- Integrating third-party IoT protocol libraries
- CMake project organization
