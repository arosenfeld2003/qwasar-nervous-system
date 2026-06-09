# Nervous System — Engineering Lab 4

A unified alarm system for smart homes and organizations. Devices communicate over heterogeneous protocols (MQTT, Zigbee, Z-Wave, HTTP); this system reads, interprets, and responds to their events in real-time.

## Team

| Developer | Language | Primary Ownership |
|-----------|----------|-------------------|
| Alex      | Rust     | Event Queue, Alarm Dispatcher |
| Tim       | C++      | Protocol Adapters, Processing Engine |

## System Components

```
[MQTT] [Zigbee] [Z-Wave] [HTTP]
         |
    [Input Adapters]       ← C++
         |
  [Unified Event Queue]    ← Rust
         |
  [Processing Engine]      ← C++
         |
 [Alarm Dispatcher]        ← Rust
         |
[E-mail] [SMS] [HTTP push]
```

## Constraints

- **Real-time**: events processed and alarms triggered with minimal latency
- **Scalable**: handles high device/event volume
- **Reliable**: fault-tolerant, no dropped events
- **Secure**: data security is paramount (security devices involved)
- **Extensible**: new protocol adapters plug in without rework

## Docs

- [Architecture Approaches](docs/architecture.md)
- [Language Split Strategy](docs/language-split.md)
- [Technology Choices](docs/technologies.md)

## Getting Started

### Quick Start: Run the Demo

The demo CLI simulates a full end-to-end pipeline with synthetic events:

```bash
# Terminal 1: Start the full pipeline
make demo

# Terminal 2: Start the webhook listener (to see alarm notifications)
make webhook-listener
```

The demo publishes simulated device events and shows:
- `[EVENT]` lines as events flow through the pipeline
- `[ALARM]` lines as the processing engine triggers alarms
- Webhook notifications appear in Terminal 2

**Single event injection:**
```bash
cargo run -p demo-cli --manifest-path rust/Cargo.toml -- \
  --event smoke_detected --count 1
```

### Run Tests

```bash
# Test all Rust components
make test-all

# Or run individually
cargo test --manifest-path rust/Cargo.toml --workspace
```

## Architecture

The pipeline is split between C++ (protocol I/O and rule processing) and Rust (event broker and dispatch):

1. **Protocol Adapters (C++)** — HTTP, MQTT, Zigbee, Z-Wave simulators → publish raw events to RabbitMQ
2. **Queue Service (Rust)** — consumes raw events, persists to MongoDB, republishes normalized
3. **Processing Engine (C++)** — evaluates events against 5 rules, produces alarm decisions
4. **Dispatcher (Rust)** — consumes alarms, sends via webhook and/or email

Events flow through RabbitMQ queues:
- `device.events.raw` → queue service → `device.events.normalized`
- `device.events.normalized` → processing engine → `alarms.dispatch`
- `alarms.dispatch` → dispatcher → webhook/email

## Documentation

- [Architecture Approaches](docs/architecture.md)
- [Language Split Strategy](docs/language-split.md)
- [Technology Choices](docs/technologies.md)
- [Raspberry Pi Zigbee Deployment](specs/zigbee_pi_deployment.md)
