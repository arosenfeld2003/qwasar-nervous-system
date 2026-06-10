# Nervous System — Engineering Lab 4

A unified alarm system for smart homes and organizations. Devices communicate over heterogeneous protocols (MQTT, Zigbee, Z-Wave, HTTP); this system reads, interprets, and responds to their events in real-time.

| Developer | Language | Components |
|-----------|----------|-----------|
| Alex | Rust | Event Queue, Alarm Dispatcher |
| Tim | C++ | Protocol Adapters, Processing Engine |

## Architecture

### Current Pipeline

```
[MQTT Device]  [Zigbee Device]  [Z-Wave Device]  [HTTP Device]
       \              |                |               /
        └─────────────┴────────────────┴──────────────┘
                   [Protocol Adapters]  ← C++ (Tim)
                           │
                    [RabbitMQ Broker]
                    device.events.raw
                           │
                   [Queue Service]      ← Rust (Alex)
                  (validates, persists)
                           │
               device.events.normalized
                           │
                [Processing Engine]     ← C++ (Tim)
                  (evaluates rules)
                           │
                    alarms.dispatch
                           │
                  [Alarm Dispatcher]    ← Rust (Alex)
                           │
              ┌────────────┴────────────┐
           [Webhook]               [Email/SMS]
```

### Raspberry Pi Integration (Next Step)

```
[Zigbee Sensor] ──USB dongle──→ [Raspberry Pi]
                                 C++ Adapter
                                      │  TCP
                                 [RabbitMQ]  ← existing pipeline continues
```

See [Raspberry Pi Deployment](specs/zigbee_pi_deployment.md) for details.

## Quick Start

### Prerequisites

- **Docker** — running (start Docker Desktop on macOS)
- **Rust** — install from https://rustup.rs
- **CMake** (optional) — for C++ processing engine: `cmake --version`

### Run the Demo

Terminal 1 — start the pipeline:
```bash
make demo
```

Terminal 2 — listen for alarm notifications:
```bash
make webhook-listener
```

The demo publishes simulated device events and shows:
- `[EVENT]` lines as events flow through the pipeline
- `[ALARM]` lines when the processing engine detects a trigger

## Triggering Detections

Simulate a specific event to watch it propagate through the system:

### Smoke Detection (Fire Alarm)

**Terminal 1:**
```bash
cargo run -p demo-cli -- --event smoke_detected --count 1
```

**What you'll see:**
- Demo-cli prints: `[EVENT] smoke_detected from sim-mqtt-002 (MQTT)`
- Processing engine matches the `fire_rule` and generates an alarm
- Webhook listener prints: `[ALARM] Fire severity=Critical from sim-mqtt-002`

### Flood Detection (Flood Alarm)

**Terminal 1:**
```bash
cargo run -p demo-cli -- --event flood_detected --count 1
```

**What you'll see:**
- Demo-cli prints: `[EVENT] flood_detected from sim-zigbee-002 (Zigbee)`
- Processing engine matches the `flood_rule` and generates an alarm
- Webhook listener prints: `[ALARM] Flood severity=High from sim-zigbee-002`

### Other Events

| Event | Protocol | Rule Fired | Severity |
|-------|----------|-----------|----------|
| `smoke_detected` | MQTT | `fire_rule` | Critical |
| `flood_detected` | Zigbee | `flood_rule` | High |
| `motion_detected` | MQTT | `motion_rule` | Low |
| `door_opened` | Zigbee | `intrusion_rule` | Medium |
| `temperature_reading` | HTTP | _(no match)_ | — |

Try each one: `cargo run -p demo-cli -- --event <event_type> --count 1`

## Running Tests

```bash
make test-all
```

Runs all Rust unit tests. C++ tests require cmake.

## Documentation

- [Architecture & Design Decisions](docs/architecture.md)
- [Language Split & Integration](docs/language-split.md)
- [Technology Choices](docs/technologies.md)
- [Raspberry Pi Zigbee Deployment](specs/zigbee_pi_deployment.md)
