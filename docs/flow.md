# End-to-End Event Flow

## System Architecture

```mermaid
%%{init: {"theme": "default", "themeVariables": {"fontSize": "18px"}}}%%
flowchart TD
    CLI["🖥️  demo-cli\n(Rust)"]

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
    WH["📡 Webhook Listener\nlocalhost:8099/alarm"]

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
%%{init: {"theme": "default", "themeVariables": {"fontSize": "18px"}}}%%
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
    Q->>Q: validate + persist to MongoDB
    Q->>PE: device.events.normalized

    Note over PE,D: Phase 2 · Rule Evaluation → Dispatch

    PE->>PE: rule-003 matches smoke_detected<br/>→ Fire / Critical
    PE->>D: alarms.dispatch<br/>{ alarm_type: Fire, severity: Critical }
    D->>WH: POST /alarm
    WH-->>Dev: 💌 Fire · Critical · sim-mqtt-002
```

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
