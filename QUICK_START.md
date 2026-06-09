# Quick Start Guide

## For the Impatient

### Run the Full Demo (3 commands, 2 terminals)

**Terminal 1:**
```bash
make demo
```

**Terminal 2** (optional but recommended):
```bash
make webhook-listener
```

That's it. You'll see:
- Events flowing through the system (`[EVENT]` lines)
- Alarms triggering (`[ALARM]` lines)
- Webhook notifications (in Terminal 2)

Press Ctrl-C to stop.

### Run All Tests

```bash
make test-all
```

### Single Smoke Test

```bash
cargo run -p demo-cli --manifest-path rust/Cargo.toml -- \
  --event smoke_detected --count 1
```

This publishes one smoke event and waits for the critical fire alarm.

---

## What Each Command Does

| Command | What it does | Terminal |
|---------|-------------|----------|
| `make demo` | Start full pipeline + demo CLI | 1 |
| `make webhook-listener` | Start webhook notification receiver | 2 |
| `make test-all` | Run all unit tests | Any |
| `Ctrl-C` in demo | Stop event publishing | 1 |
| `make dev-down` | Shut down Docker services | Any |

---

## File Locations

- **Demo CLI source:** `rust/demo-cli/src/main.rs`
- **Webhook listener source:** `rust/demo-cli/src/bin/webhook_listener.rs`
- **Test cases:** 
  - `rust/queue/src/main.rs` (lines with `#[cfg(test)]`)
  - `rust/dispatcher/src/main.rs` (lines with `#[cfg(test)]`)
- **Rules configuration:** `cpp/processing/config/rules.json`
- **Event schema:** `schema/event.json`

---

## Event Types to Test

Push any of these through the system:

```bash
cargo run -p demo-cli --manifest-path rust/Cargo.toml -- \
  --event <TYPE> --count 1
```

Replace `<TYPE>` with:
- `motion_detected` → Motion alarm
- `door_opened` → Intrusion alarm
- `smoke_detected` → 🔴 Critical Fire alarm
- `flood_detected` → Flood alarm
- `temperature_reading` → No alarm (normal data)

---

## Expected Output

### Terminal 1 (Demo CLI)

```
[EVENT] sim-mqtt-002 | smoke_detected | Mqtt

[ALARM] fire | 🔴 CRITICAL 🔴 | Critical fire alarm: smoke detected | sim-mqtt-002
```

### Terminal 2 (Webhook Listener)

```
💌 [WEBHOOK] Received alarm: Fire | Critical | Critical fire alarm: smoke detected | sim-mqtt-002
```

---

## Troubleshooting

**"connection refused"**
→ Docker isn't running. Start Docker and run `make demo` again.

**"cmake not found"**
→ C++ processing engine won't run, but Rust pipeline works (no alarms generated).

**No alarms appearing**
→ Check that all services started in `make demo` output. Events are published but rules don't match. Try `--event smoke_detected`.

**Webhook listener not receiving alarms**
→ Make sure Terminal 2 started before events were published. Or restart demo.

---

## Next Steps

- **Full demo guide:** Read `DEMO.md`
- **Technical details:** Read `IMPLEMENTATION_SUMMARY_DEMO.md`
- **Architecture:** Read `docs/architecture.md`
- **Add custom rules:** Edit `cpp/processing/config/rules.json`

---

## System Requirements

- Docker & Docker Compose
- Rust toolchain (`cargo`)
- ~100MB disk space for binaries and Docker images
- Ports 5672 (RabbitMQ), 27017 (MongoDB), 8099 (webhook listener) available

No CMake required (C++ processing engine optional).
