# End-to-End Event Flow

## System Architecture

```mermaid
flowchart TD
    CLI["demo-cli\n(Rust)"]

    subgraph BROKER["RabbitMQ  (port 5672)"]
        RAW["device.events.raw"]
        NORM["device.events.normalized"]
        AQ["alarms.dispatch"]
    end

    subgraph SERVICES["Pipeline Services"]
        Q["Queue Service  (Rust)\nvalidates · persists to MongoDB"]
        PE["Processing Engine  (C++)\nevaluates rules.json"]
        D["Dispatcher  (Rust)\nHTTP webhook · email"]
    end

    DB[("MongoDB\n:27017")]
    WH["Webhook Listener\nlocalhost:8099/alarm"]

    CLI -->|"publish event"| RAW
    RAW -->|"consume"| Q
    Q -->|"persist"| DB
    Q -->|"republish"| NORM
    NORM -->|"consume"| PE
    PE -->|"alarm decision"| AQ
    AQ -->|"consume"| D
    D -->|"POST /alarm"| WH
```

---

## Phase-by-Phase: smoke_detected

```mermaid
sequenceDiagram
    actor Dev
    participant CLI as demo-cli
    participant Q as Queue Service
    participant PE as Processing Engine
    participant D as Dispatcher
    participant WH as Webhook

    Dev->>CLI: --event smoke_detected --count 1

    Note over CLI,Q: Phase 1 · Injection → Validation

    CLI->>Q: device.events.raw<br/>{ event_type: smoke_detected, device: sim-mqtt-002 }
    Q->>Q: deserialize into DeviceEvent struct<br/>reject + nack if schema invalid
    Q->>Q: insert doc into nervous_system.events<br/>failure logged but event is not dropped
    Q->>PE: device.events.normalized<br/>forward_event() — delivery_mode=2 (persistent)

    Note over PE,D: Phase 2 · Rule Evaluation → Dispatch

    PE->>PE: rule-003 matches smoke_detected<br/>→ Fire / Critical
    PE->>D: alarms.dispatch<br/>{ alarm_type: Fire, severity: Critical }
    D->>WH: POST /alarm
    WH-->>Dev: Fire / Critical / sim-mqtt-002
```

**Queue service — persist & republish** (`rust/queue/src/main.rs`):

```rust
// persist to MongoDB — failure is logged but does not drop the event
if let Err(e) = events_col.insert_one(doc).await {
    warn!("MongoDB insert failed (event not dropped): {}", e);
}

forward_event(&publish_channel, &event).await?;
delivery.ack(BasicAckOptions::default()).await?;

// ---

// forward_event: republish the same DeviceEvent to device.events.normalized
// delivery_mode=2 marks the message as persistent on the broker
async fn forward_event(channel: &lapin::Channel, event: &DeviceEvent) -> Result<()> {
    let payload = serde_json::to_vec(event)?;
    channel
        .basic_publish("", OUTBOUND_QUEUE, BasicPublishOptions::default(),
            &payload, lapin::BasicProperties::default().with_delivery_mode(2))
        .await??;
    Ok(())
}
```

The event is acknowledged only after both the MongoDB write and the republish succeed. If either fails, the broker redelivers the message.

---

## Rule Reference

| Rule | Trigger Event | Condition | Alarm | Severity |
|------|--------------|-----------|-------|----------|
| rule-001 | `motion_detected` | zone = front-door | Motion | High |
| rule-002 | `motion_detected` | zone = back-yard | Motion | Medium |
| rule-003 | `smoke_detected` | _(any)_ | Fire | Critical |
| rule-004 | `door_opened` | _(any)_ | Intrusion | High |
| rule-005 | `flood_detected` | _(any)_ | Flood | High |

`temperature_reading` — no rule match, no alarm.

---

## Demo Commands

```bash
make services        # start the pipeline
make webhook-listener  # terminal 2

# inject events (from project root)
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event smoke_detected --count 1
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event flood_detected --count 1
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event motion_detected --count 1
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event door_opened --count 1
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event temperature_reading --count 1

make stop            # shut everything down
```
