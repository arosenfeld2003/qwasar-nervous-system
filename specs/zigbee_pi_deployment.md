# Zigbee Adapter: Raspberry Pi Distributed Deployment Specification

## 1. Overview
This document outlines the architecture, requirements, and implementation strategy for a distributed deployment of the Nervous System project. In this setup, a Raspberry Pi acts as a dedicated Edge Gateway. It interfaces with physical Zigbee hardware using a C++ protocol adapter and forwards normalized events over the local network to a central RabbitMQ broker.

## 2. Architecture Layout

```text
[ Physical Devices ]        [ Edge Gateway (Raspberry Pi) ]             [ Main Server / Laptop ]

  Zigbee Sensor             USB Zigbee Dongle                           RabbitMQ Broker (Docker)
       |                            |                                           |
       | (Zigbee Radio)             | (Serial /dev/ttyACM0)                     |
       v                            v                                           v
[ Coordinator ] ----------> [ C++ Zigbee Adapter ] ---(AMQP over TCP)---> [ Rust Event Queue ]
                                    |                                           |
                            (Translates to JSON)                        [ Alarm Dispatcher ]
```

## 3. Hardware Requirements
* **Edge Gateway:** Raspberry Pi (Model 3B+, 4, or 5 recommended).
* **Zigbee Coordinator:** A USB Zigbee Dongle (e.g., Sonoff Zigbee 3.0 USB Dongle Plus, ConBee II, or Nortek GoControl).
* **Test Device:** At least one Zigbee end-device (e.g., a door/window sensor, motion sensor, or smart button).
* **Network:** Both the Raspberry Pi and the Main Server must be on the same local network (Wi-Fi or Ethernet).

## 4. Software Dependencies (Raspberry Pi)
To run the C++ adapter on the Pi, the following will be required:
* **Build System:** `gcc`/`g++` (C++17/20), `cmake`, `make`.
* **AMQP Client:** A C++ library to publish to RabbitMQ (e.g., `AMQP-CPP` or `rabbitmq-c`).
* **JSON Parser:** `nlohmann/json` for generating the normalized event schema.
* **Zigbee Interfacing:** 
  * *Option A (Direct Serial):* Use a C++ serial library (like `Boost.Asio`) to read directly from the dongle using its native serial protocol (e.g., TI Z-Stack or Silicon Labs EZSP).
  * *Option B (C/C++ SDK):* Use a library like the ZBOSS SDK if compatible.
  * *Option C (Hybrid/Daemon):* Run `zigbee2mqtt` on the Pi to handle the complex serial protocols, and have the C++ Adapter consume local MQTT messages, translate them to the Nervous System JSON schema, and forward them to the central RabbitMQ broker.

## 5. C++ Adapter Implementation Details

The C++ executable running on the Pi should implement the following lifecycle:

1. **Initialization:**
   * Load configuration (Broker IP address, port, credentials).
   * Open connection to the central RabbitMQ broker over the network.
   * Open the serial port to the Zigbee USB dongle (e.g., `/dev/ttyUSB0` or `/dev/ttyACM0`).
2. **Event Loop:**
   * Continuously poll or asynchronously read from the Zigbee dongle.
3. **Translation Phase:**
   * Parse the incoming raw Zigbee frames (e.g., ZCL Attribute Reporting for temperature or motion).
   * Map the hardware-specific payload into the shared `event.json` schema.
4. **Publish Phase:**
   * Serialize the JSON.
   * Publish the message to the `incoming_events` exchange on the RabbitMQ broker.

### Example JSON Payload Output
```json
{
  "device_id": "zigbee-motion-hallway",
  "protocol": "zigbee",
  "event_type": "motion_detected",
  "timestamp_ms": 1713480500000,
  "payload": {
    "occupancy": true,
    "battery_percent": 85
  }
}
```

## 6. Deployment & Build Strategy
* **Compilation:** It is highly recommended to compile the C++ adapter directly on the Raspberry Pi. Setting up a cross-compilation toolchain (compiling ARM64 binaries on an x86/ARM Mac) can be complex and error-prone for a lab environment.
* **Process Management:** The C++ binary should be run via `systemd` or `supervisor` on the Pi to ensure it automatically restarts if it crashes or if the Pi reboots.
* **Security Constraints:** The RabbitMQ broker on the main server must be configured to accept connections from external IP addresses (by default, it may only bind to `localhost`). You will need to create a dedicated AMQP user with limited permissions for the Pi to use.
