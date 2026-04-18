# Technology Choices

## Message Broker

| Option | Pros | Cons | Verdict |
|--------|------|------|---------|
| **RabbitMQ** | Simple setup, AMQP standard, great for task queues | Less suited for log-style replay | Good default for this lab |
| **Kafka** | High throughput, log retention, replay events | Heavier ops, Zookeeper dependency | Better if event replay is needed |
| **Redis Streams** | Lightweight, easy local dev | Less mature ecosystem | Good for prototyping |

**Start with RabbitMQ**; the AMQP protocol has solid client libraries for both Rust (`lapin`) and C++ (`amqpcpp`).

## Databases

| Purpose | Choice | Rationale |
|---------|--------|-----------|
| Event log / history | **MongoDB** | Schemaless; events naturally fit document model |
| Rule store / config | **Redis** | Fast key-value; rules hot-reloaded without restart |
| Time-series metrics | **DynamoDB** or **InfluxDB** | TTL support, optimized for time-range queries |

## Protocol Libraries (C++)

| Protocol | Library | Notes |
|----------|---------|-------|
| MQTT     | `Paho MQTT C++` | Apache, well-maintained |
| Zigbee   | `zigpy` C bindings or ZBOSS SDK | May require hardware dongle for real testing |
| Z-Wave   | `OpenZWave` | Mature, active community |
| HTTP     | `cpp-httplib` or `Boost.Beast` | cpp-httplib is simpler for adapters |

## Rust Crates (Event Queue + Dispatcher)

| Purpose | Crate | Notes |
|---------|-------|-------|
| Async runtime | `tokio` | Industry standard |
| AMQP (RabbitMQ) | `lapin` | Tokio-native |
| JSON | `serde_json` | Pairs with `serde` derive macros |
| HTTP alarms | `reqwest` | Async HTTP client |
| Email alarms | `lettre` | SMTP + async support |
| Config | `config` + `serde` | Layered config (file + env) |
| Logging | `tracing` | Structured async-aware logs |

## Infrastructure

| Tool | Purpose |
|------|---------|
| Docker + Docker Compose | Local dev: broker, databases, services |
| Kubernetes (stretch goal) | Production-like deployment |
| GitHub Actions | CI: build + test both C++ and Rust |

## Simulating IoT Devices (for development)

Since physical Zigbee/Z-Wave hardware isn't required for the lab, consider:
- **MQTT**: `mosquitto_pub` CLI to publish mock events
- **HTTP**: `curl` or a small Python script to POST events
- **Zigbee/Z-Wave**: Mock adapter in C++ that generates events on a timer
