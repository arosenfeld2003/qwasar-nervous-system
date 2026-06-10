# Technology Choices

## Message Broker: RabbitMQ

AMQP protocol with solid Rust (`lapin`) and C++ (`amqpcpp`) clients. Supports the five queue topics (raw events, normalized, alarms) with simple topic-based routing.

## Databases

| Purpose | Technology | Rationale |
|---------|-----------|-----------|
| Event History | **MongoDB** | Schemaless documents fit event logs naturally |
| Rule Configuration | **Redis** | Fast KV store for rule deployment and caching |

## C++ Protocol Libraries

| Protocol | Library |
|----------|---------|
| MQTT | Paho MQTT C++ |
| Zigbee | ZBOSS SDK or zigpy C bindings |
| Z-Wave | OpenZWave |
| HTTP | cpp-httplib |

## Rust Crates

| Purpose | Crate |
|---------|-------|
| Async Runtime | `tokio` |
| RabbitMQ | `lapin` (Tokio-native) |
| JSON | `serde` + `serde_json` |
| HTTP Webhooks | `reqwest` |
| Email Alerts | `lettre` |
| Logging | `tracing` |

## Infrastructure

- **Docker Compose**: Local dev (RabbitMQ, MongoDB, Redis, PostgreSQL for Temporal)
- **CMake**: C++ builds
- **Cargo**: Rust builds and tests
