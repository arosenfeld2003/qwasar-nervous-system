# Nervous System Demo & Testing Guide

This document explains how to run the demo, execute tests, and verify the full pipeline.

## Prerequisites

- Docker & Docker Compose (for RabbitMQ and MongoDB)
- Rust toolchain (cargo, rustc)
- Optional: CMake (for C++ processing engine integration)

## Running the Demo

The demo simulates a complete end-to-end pipeline with synthetic device events.

### Step 1: Start the Full Pipeline

In **Terminal 1**:
```bash
make demo
```

This command:
1. Starts Docker infrastructure (RabbitMQ on 5672, MongoDB on 27017)
2. Builds the C++ processing engine (if cmake available)
3. Launches Rust services in background:
   - Queue service (processes `device.events.raw` → `device.events.normalized`)
   - Dispatcher (sends `alarms.dispatch` notifications)
   - C++ Processing Engine (evaluates rules and produces alarms)
4. Starts the demo CLI, which publishes synthetic events every 1.5 seconds

Expected output in Terminal 1:
```
═══════════════════════════════════════════════════════════
  Nervous System Demo CLI
═══════════════════════════════════════════════════════════
  AMQP URI: amqp://guest:guest@localhost:5672
  Interval: 1500ms
  Publishing continuously (Ctrl-C to stop)
═══════════════════════════════════════════════════════════

[EVENT] sim-mqtt-001 | motion_detected | Mqtt

[EVENT] sim-zigbee-001 | door_opened | Zigbee

[ALARM] motion | HIGH | Motion detected in front-door zone | sim-mqtt-001

[ALARM] intrusion | HIGH | Intrusion detected: door opened | sim-zigbee-001

[EVENT] sim-http-001 | temperature_reading | Http

[ALARM] fire | 🔴 CRITICAL 🔴 | Critical fire alarm: smoke detected | sim-mqtt-002
```

### Step 2: Monitor Webhook Notifications

In **Terminal 2** (optional, but recommended):
```bash
make webhook-listener
```

Expected output:
```
📡 Webhook listener running on http://0.0.0.0:8099/alarm
waiting for alarm notifications...

💌 [WEBHOOK] Received alarm: Intrusion | High | Intrusion detected: door opened | sim-zigbee-001

💌 [WEBHOOK] Received alarm: Fire | Critical | Critical fire alarm: smoke detected | sim-mqtt-002
```

### Step 3: Stop the Demo

In **Terminal 1**, press `Ctrl-C` to stop the demo CLI. Services will continue running in the background.

To clean up everything:
```bash
make dev-down
```

## Testing

### Run All Tests

```bash
make test-all
```

This executes:
- All Rust unit tests (queue service, dispatcher, common types)
- C++ unit tests for the processing engine (if cmake available)

Expected output:
```
running 2 tests
test tests::test_config_no_channels_warns ... ok
test tests::test_alarm_decision_serialization ... ok

running 2 tests
test tests::test_event_deserialization ... ok
test tests::test_invalid_event_rejected ... ok

✅ Rust tests passed!
```

### Run Rust Tests Only

```bash
cargo test --manifest-path rust/Cargo.toml --workspace
```

### Run C++ Tests Only

```bash
make build-cpp
./cpp/build/processing/test_processing_engine
```

## Demo CLI Options

Inject specific events or control pipeline behavior:

### Publish a single smoke event and see the alarm
```bash
cargo run -p demo-cli --manifest-path rust/Cargo.toml -- \
  --event smoke_detected --count 1
```

### Publish 10 motion events at 2-second intervals
```bash
cargo run -p demo-cli --manifest-path rust/Cargo.toml -- \
  --event motion_detected --count 10 --interval-ms 2000
```

### Publish with verbose JSON output
```bash
cargo run -p demo-cli --manifest-path rust/Cargo.toml -- \
  --verbose --count 5
```

### Available event types
- `motion_detected` — triggers motion alarm if zone is "front-door" or "back-yard"
- `door_opened` — triggers intrusion alarm
- `smoke_detected` — triggers critical fire alarm
- `flood_detected` — triggers flood alarm
- `temperature_reading` — no alarm (normal data)

## Event Flow Walkthrough

When you run `make demo`, events flow through:

1. **Demo CLI** publishes a `DeviceEvent` (e.g., `smoke_detected`) to RabbitMQ queue `device.events.raw`

2. **Queue Service (Rust)** consumes the event, stores it in MongoDB, and republishes to `device.events.normalized`

3. **Processing Engine (C++)** consumes the normalized event, evaluates it against 5 rules in `cpp/processing/config/rules.json`:
   - Rule `rule-001`: motion at "front-door" → high motion alarm
   - Rule `rule-002`: motion at "back-yard" → medium motion alarm
   - Rule `rule-003`: smoke → critical fire alarm ⚠️
   - Rule `rule-004`: door_opened → high intrusion alarm
   - Rule `rule-005`: flood → high flood alarm

4. **Processing Engine** publishes matching `AlarmDecision` to RabbitMQ queue `alarms.dispatch`

5. **Dispatcher (Rust)** consumes alarms and sends notifications:
   - HTTP webhook POST to `http://localhost:8099/alarm` (visible in Terminal 2)
   - Email via SMTP (if `WEBHOOK_URL` is set; requires SMTP config)

## Troubleshooting

### "connection refused" errors
- Check that Docker is running: `docker ps`
- Ensure RabbitMQ is accessible: `docker compose logs rabbitmq`

### Queue service doesn't start
- Check MongoDB is running: `docker compose logs mongodb`
- Verify AMQP connection: logs in `/tmp/queue.log`

### Processing engine not running
- If cmake is not available, it won't be started. Install cmake or use Rust-only mode (events won't generate alarms, but queue will work)
- Check C++ build errors: look at logs from the build output

### Webhook listener doesn't receive alarms
- Ensure `WEBHOOK_URL=http://localhost:8099/alarm` is set (it is by default in `make demo`)
- Check dispatcher is running: `ps aux | grep dispatcher`

## Advanced Usage

### Custom Rules

Edit `cpp/processing/config/rules.json` to add new rules. The rule DSL supports:
- Field matching: `{"device_id": "sim-mqtt-001"}`
- Numeric operators: `{temperature: {$gt: 30}}`
- Logical operators: `{$all: [...]}`, `{$any: [...]}`, `{$not: {...}}`

After changes, restart the processing engine:
```bash
make dev-down
make demo
```

### Inspect MongoDB Events

```bash
# Connect to MongoDB
docker exec -it <mongodb-container> mongosh

# List events
use nervous_system
db.events.find().pretty()
```

### View RabbitMQ Queues

Open http://localhost:15672 (default credentials: guest/guest) to see:
- Queue depths
- Message rates
- Consumer connections

## Cleanup

To stop all services and remove containers:
```bash
make dev-down
```

To remove build artifacts:
```bash
make clean-cpp
cargo clean --manifest-path rust/Cargo.toml
```
