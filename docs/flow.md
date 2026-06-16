# End-to-End Event Flow

This diagram traces a `smoke_detected` event from injection through alarm delivery. The same path applies to all event types — only the rule matched and alarm output differ.

```mermaid
sequenceDiagram
    actor Dev as Developer
    participant CLI as demo-cli<br/>(Rust)
    participant RAW as RabbitMQ<br/>device.events.raw
    participant Q as Queue Service<br/>(Rust)
    participant DB as MongoDB
    participant NORM as RabbitMQ<br/>device.events.normalized
    participant PE as Processing Engine<br/>(C++)
    participant DISP_Q as RabbitMQ<br/>alarms.dispatch
    participant D as Dispatcher<br/>(Rust)
    participant WH as Webhook Listener

    Dev->>CLI: cargo run -p demo-cli<br/>-- --event smoke_detected --count 1

    Note over CLI,RAW: Phase 1 — Event Injection

    CLI->>RAW: publish DeviceEvent JSON<br/>{ device_id: "sim-mqtt-002",<br/>  protocol: "mqtt",<br/>  event_type: "smoke_detected",<br/>  payload: { zone: "kitchen" } }

    Note over Q,DB: Phase 2 — Validation & Persistence

    RAW->>Q: consume event
    Q->>Q: validate JSON schema
    Q->>DB: persist to nervous_system.events
    Q->>NORM: republish normalized event

    Note over PE,DISP_Q: Phase 3 — Rule Evaluation

    NORM->>PE: consume normalized event
    PE->>PE: match event_type == "smoke_detected"<br/>→ rule-003 matches (no condition required)
    PE->>DISP_Q: publish AlarmDecision<br/>{ alarm_type: "Fire",<br/>  severity: "Critical",<br/>  message: "Smoke detected — possible fire",<br/>  device_id: "sim-mqtt-002" }

    Note over D,WH: Phase 4 — Alarm Dispatch

    DISP_Q->>D: consume alarm
    D->>WH: POST /alarm<br/>{ alarm_type: "Fire", severity: "Critical", ... }
    WH-->>Dev: 💌 [WEBHOOK] Fire | Critical |<br/>Smoke detected — possible fire | sim-mqtt-002
```

## Rule Reference

The processing engine evaluates events against 5 rules loaded from `cpp/processing/config/rules.json`:

| Rule | Event Type | Condition | Alarm Type | Severity |
|------|-----------|-----------|-----------|----------|
| rule-001 | `motion_detected` | `zone == "front-door"` | Motion | High |
| rule-002 | `motion_detected` | `zone == "back-yard"` | Motion | Medium |
| rule-003 | `smoke_detected` | _(any)_ | Fire | Critical |
| rule-004 | `door_opened` | _(any)_ | Intrusion | High |
| rule-005 | `flood_detected` | _(any)_ | Flood | High |

`temperature_reading` has no matching rule — events flow through the pipeline and are persisted but produce no alarm.

## Service Connection Map

```mermaid
graph LR
    subgraph Injection
        CLI[demo-cli<br/>Rust]
    end

    subgraph Broker["RabbitMQ (Docker :5672)"]
        RAW[device.events.raw]
        NORM[device.events.normalized]
        AQ[alarms.dispatch]
    end

    subgraph Processing
        Q[Queue Service<br/>Rust — :5672]
        PE[Processing Engine<br/>C++ — reads rules.json]
        D[Dispatcher<br/>Rust — WEBHOOK_URL]
    end

    subgraph Storage
        DB[(MongoDB<br/>:27017)]
    end

    subgraph Output
        WH[Webhook Listener<br/>:8099/alarm]
    end

    CLI -->|publish| RAW
    RAW -->|consume| Q
    Q -->|persist| DB
    Q -->|republish| NORM
    NORM -->|consume| PE
    PE -->|alarm decision| AQ
    AQ -->|consume| D
    D -->|HTTP POST| WH
```

## Demo Commands

```bash
# Start the pipeline
make services

# Terminal 2: receive alarm notifications
make webhook-listener

# Inject events (from project root)
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event smoke_detected --count 1
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event flood_detected --count 1
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event motion_detected --count 1
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event door_opened --count 1
cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event temperature_reading --count 1  # no alarm

# Stop everything
make stop
```
