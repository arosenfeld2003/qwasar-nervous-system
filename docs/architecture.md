# Architecture Approaches

## Option A — Shared-Memory Pipeline (Monorepo, IPC via shared memory)

Both the C++ adapters and the Rust queue/dispatcher run as separate OS processes, communicating via a shared memory ring buffer (e.g., POSIX shared memory or `mmap`).

**Pros**
- Zero-copy event passing between processes → lowest latency
- Each language component can be built and tested independently
- Clean fault boundary: a crashed adapter doesn't corrupt the queue

**Cons**
- Shared memory is tricky to synchronize safely (need careful use of atomics or a lock)
- Harder to run across machines — this approach is single-host only
- Debugging cross-language memory layout mismatches can be painful

**Good fit when**: latency is the top priority and the system runs on a single host.

---

## Option B — Message Broker (RabbitMQ or Kafka as the integration layer)

C++ adapters publish normalized events to a broker topic. Rust consumes from the broker, evaluates rules, and dispatches alarms.

**Pros**
- Adapters and dispatcher are fully decoupled — independent deploy/restart
- Broker provides durability (no dropped events on crash)
- Scales horizontally: add more adapter instances or consumer instances
- Each team works against a well-defined message schema (e.g., protobuf or JSON)

**Cons**
- Broker is an additional infrastructure dependency
- Adds latency vs. shared memory (~1–5 ms per hop)
- Requires a running broker for local dev (Docker Compose helps)

**Good fit when**: reliability and scalability matter more than raw latency — likely the right default for this lab.

---

## Option C — Embedded Queue (Single binary via FFI)

One master binary (Rust) that `dlopen`s or statically links C++ adapter code via a C FFI boundary. The queue lives in Rust; C++ calls into it via a C API.

**Pros**
- Single deployable artifact
- No IPC overhead — direct function calls across the FFI boundary
- Simpler ops story

**Cons**
- FFI boundary between C++ and Rust is boilerplate-heavy
- Memory safety is opt-out across the boundary — requires `unsafe` in Rust
- Build system complexity: Cargo + CMake integration is non-trivial
- Tight coupling makes independent team iteration harder

**Good fit when**: deployment simplicity is a hard requirement and teams are comfortable with build tooling.

---

## Recommendation

**Start with Option B** (broker-based). It maps cleanly to the lab's stated stack (RabbitMQ/Kafka), supports the two-developer workflow naturally, and mirrors real-world IoT platform architectures. Option A could be explored as a latency optimization later.

### Event Schema (shared contract between C++ and Rust)

```json
{
  "device_id": "zigbee-sensor-a1b2",
  "protocol": "zigbee",
  "event_type": "motion_detected",
  "timestamp_ms": 1713480000000,
  "payload": { "zone": "front-door" }
}
```

Both sides agree on this schema; Rust's `serde_json` and C++'s `nlohmann/json` can both parse it with minimal ceremony.
