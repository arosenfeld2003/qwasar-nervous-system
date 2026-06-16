# Dependencies

## Infrastructure

### RabbitMQ
Message broker at the center of the pipeline. All services communicate exclusively through it — no direct service-to-service calls. Chosen over Kafka for simplicity (no ZooKeeper, easy Docker setup) and over Redis Streams for its stronger AMQP delivery guarantees (persistent queues, explicit ack/nack).

### MongoDB
Event store. The queue service persists every inbound `DeviceEvent` to `nervous_system.events`. Schema-less documents are a natural fit for events whose payloads vary by protocol. Persistence is best-effort — a failed write logs a warning but does not drop the event.

### Docker Compose
Runs RabbitMQ and MongoDB locally (`docker/docker-compose.yml`). No local installation required for either service.

---

## Rust Crates

### `lapin`
AMQP 0-9-1 client for RabbitMQ. Used by the queue service and dispatcher to consume and publish messages. Tokio-native — fits the async runtime without bridging. The `delivery.ack()` / `delivery.nack()` API is the core of the at-least-once delivery contract.

### `tokio`
Async runtime. All three Rust services (queue, dispatcher, demo-cli) run on tokio. Enables concurrent consume + publish loops without threads.

### `serde` + `serde_json`
Serialization layer. `DeviceEvent` and `AlarmDecision` derive `Serialize`/`Deserialize` — converting to/from JSON for RabbitMQ payloads and MongoDB documents requires no manual mapping.

### `reqwest`
HTTP client used by the dispatcher to POST alarm decisions to the webhook URL (`WEBHOOK_URL` env var).

### `lettre`
SMTP email client. Used by the dispatcher as a second notification channel alongside the webhook. Both channels are optional at runtime.

### `axum`
HTTP server framework. Powers the `webhook_listener` binary — a minimal `POST /alarm` endpoint used during demos to receive and print dispatcher notifications.

### `clap`
CLI argument parsing for `demo-cli`. Provides `--event`, `--count`, `--interval-ms`, and `--verbose` flags with derived help text.

### `tracing` + `tracing-subscriber`
Structured logging across all Rust services. Outputs timestamped log lines with fields (e.g. `device_id`, `event_type`). Filter level controlled via `RUST_LOG` env var.

### `anyhow` + `thiserror`
Error handling. `anyhow` used in binaries for ergonomic `?` propagation; `thiserror` used in `nervous-system-common` to define typed error variants.

### `dotenvy`
Loads `.env` files at startup. Allows AMQP URI, MongoDB URI, webhook URL, and SMTP config to be set without exporting shell variables.

### `futures-lite`
Provides `StreamExt` for iterating over the lapin consumer stream in an async `while let` loop.

### `mongodb`
Official async MongoDB driver. Used by the queue service to insert event documents.

---

## C++ Libraries

### `rabbitmq-c`
Synchronous C AMQP client. Used by the processing engine to consume from `device.events.normalized` and publish alarm decisions to `alarms.dispatch`. Fetched via CMake `FetchContent` at build time.

### `nlohmann/json`
Header-only JSON library. Used throughout the C++ processing engine to parse inbound events and serialize alarm decisions. Chosen for its simple API (`json["key"]`) and single-header distribution.

### `cpp-httplib` (optional)
Header-only HTTP server/client library. Used by the C++ HTTP protocol adapter — the component that would receive events from HTTP-speaking devices and forward them to `device.events.raw`. Not required for the processing engine or the current demo, where `demo-cli` plays this role in software.

### `Paho MQTT C++` (optional)
MQTT protocol client library. Used by the C++ MQTT adapter — the component that would subscribe to an MQTT broker on a physical device (e.g. a Raspberry Pi with a Zigbee dongle running `zigbee2mqtt`) and publish received events to RabbitMQ. Only built if found on the system (`find_package(PahoMqttCpp QUIET)`). Not required for the current demo pipeline, where `demo-cli` simulates device events directly. Becomes necessary when connecting real hardware.
