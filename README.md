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

See individual component READMEs under `rust/` and `cpp/` once scaffolding is complete.
