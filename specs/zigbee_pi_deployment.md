# Zigbee Adapter on Raspberry Pi

Distributed architecture with a Raspberry Pi as an edge gateway for physical Zigbee devices.

## Architecture

```
[Zigbee Sensor] 
       |
    [USB Dongle on Pi]
       |
  [C++ Adapter on Pi]
       |  (AMQP over TCP)
 [RabbitMQ on Main Server]
       |
  [Rust Queue]  ← existing pipeline continues
```

## Hardware

- **Raspberry Pi**: 3B+, 4, or 5 (with USB port for dongle)
- **Zigbee Dongle**: Sonoff, ConBee II, or similar (USB)
- **Test Device**: Zigbee motion sensor, temperature sensor, or button
- **Network**: Pi and main server on same LAN

## Software on Pi

- Build: `gcc`, `g++`, `cmake`
- AMQP: `rabbitmq-c` or `AMQP-CPP`
- JSON: `nlohmann/json`
- Zigbee: Direct serial via Boost.Asio, or `zigbee2mqtt` proxy (simpler)

## C++ Adapter Lifecycle

1. **Initialize**: Connect to central RabbitMQ broker, open serial port to USB dongle
2. **Poll**: Receive Zigbee frames from dongle
3. **Translate**: Map ZCL attributes → `DeviceEvent` JSON
4. **Publish**: Send to `device.events.raw` queue

### Example Event

```json
{
  "device_id": "zigbee-motion-hallway",
  "protocol": "zigbee",
  "event_type": "motion_detected",
  "timestamp_ms": 1713480500000,
  "payload": { "occupancy": true, "battery_percent": 85 }
}
```

## Deployment

- Compile on the Pi (avoid cross-compilation complexity)
- Run via `systemd` or `supervisor` for auto-restart
- Configure RabbitMQ to accept external connections with dedicated AMQP user
