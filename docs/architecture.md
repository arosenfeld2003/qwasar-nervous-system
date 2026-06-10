# Architecture: Message Broker Pipeline

The system uses a message broker (RabbitMQ) as the integration layer between C++ protocol adapters, Rust queue/dispatcher services, and the C++ processing engine.

## Pipeline

```
[Adapters]  C++ protocol adapters (MQTT, Zigbee, Z-Wave, HTTP)
    ↓
[RabbitMQ] device.events.raw
    ↓
[Queue]     Rust service — validates, persists to MongoDB, republishes normalized
    ↓
[RabbitMQ] device.events.normalized
    ↓
[Rules]     C++ processing engine — evaluates 5 rules, produces alarm decisions
    ↓
[RabbitMQ] alarms.dispatch
    ↓
[Dispatch]  Rust service — sends via webhook and/or email
```

## Benefits

- **Decoupled**: C++ and Rust teams develop independently. Each publishes to a well-defined topic.
- **Durable**: broker ensures no dropped events if a service crashes.
- **Scalable**: add more adapter or dispatcher instances without rework.
- **Schema-based**: shared JSON schema defines the contract between all stages.

## Event Schema

All events conform to this JSON structure:

```json
{
  "device_id": "sim-mqtt-001",
  "protocol": "mqtt",
  "event_type": "smoke_detected",
  "timestamp_ms": 1718000000000,
  "payload": { "zone": "kitchen" }
}
```

Alarm decisions use:

```json
{
  "device_id": "sim-mqtt-001",
  "alarm_type": "Fire",
  "severity": "Critical",
  "message": "Smoke detected in kitchen",
  "triggered_at_ms": 1718000000010
}
```
