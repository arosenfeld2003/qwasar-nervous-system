# Language Split Strategy

## Team Ownership

| Developer | Language | Components |
|-----------|----------|-----------|
| Alex | Rust | Event Queue, Alarm Dispatcher |
| Tim | C++ | Protocol Adapters, Processing Engine |

## Why This Split

**C++ for Protocol Adapters & Processing Engine:**
- Deep IoT library ecosystem: libmosquitto (MQTT), ZBOSS (Zigbee), OpenZWave (Z-Wave), cpp-httplib (HTTP)
- Rules evaluation and ML inference have strong C++ support

**Rust for Queue & Dispatcher:**
- tokio async runtime handles high-throughput FIFO with backpressure
- reqwest (HTTP), lettre (email) cover all notification channels
- serde/serde_json simplify JSON event validation
- Memory safety guarantees prevent data races by construction

## Integration Points

Each team publishes to and consumes from well-defined RabbitMQ queues:

```
C++ Adapter → [JSON event] → device.events.raw
                                    ↓
                            Rust Queue Service
                                    ↓
Rust Dispatcher ← [JSON alarm] ← alarms.dispatch
                                    ↑
                            C++ Processing Engine
                                    ↑
                            device.events.normalized
```

Shared contract: JSON event schema defined in `architecture.md`.

## Build System

- **Rust**: Cargo workspace (queue, dispatcher, demo-cli under `rust/`)
- **C++**: CMake (processing engine under `cpp/`)
- **Local dev**: `make` targets for both

## Repository Structure

```
├── cpp/
│   ├── adapters/             # Protocol simulators
│   └── processing/           # Rules engine
├── rust/
│   ├── common/               # Shared event types
│   ├── queue/                # Event normalization
│   ├── dispatcher/           # Alarm notifications
│   └── demo-cli/             # Interactive demo
├── docker/                   # Docker Compose
├── specs/                    # Hardware/deployment specs
└── docs/                     # Architecture & design
```
