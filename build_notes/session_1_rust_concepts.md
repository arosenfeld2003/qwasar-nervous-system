# Build Notes — Session 1: Rust Services Implementation

Files built: `rust/mock-publisher`, `rust/queue` (+ MongoDB), `rust/dispatcher` (+ HTTP + email)

---

## Concept 1: `async`/`await` and `#[tokio::main]`

Every service has this at the top:

```rust
#[tokio::main]
async fn main() -> Result<()> { ... }
```

Network I/O (RabbitMQ, MongoDB, HTTP) is slow relative to CPU. Rather than blocking a thread
waiting for a response, Rust's `async` machinery lets the runtime do other work while waiting.
`tokio` is the async runtime — `#[tokio::main]` sets it up and runs the async `main` function
inside it.

An `async fn` returns a **future** — a description of work to do later. Nothing actually happens
until you `.await` it. That's why every RabbitMQ call ends with `.await?`.

---

## Concept 2: `Result<T>` and the `?` operator

Instead of exceptions, Rust uses `Result<T, E>` — a value that is either `Ok(something)` or
`Err(something)`. The `?` operator is shorthand for: *"if this is an Err, return it immediately
from the current function; otherwise unwrap the Ok value."*

```rust
let conn = Connection::connect(&amqp_uri, ConnectionProperties::default()).await?;
//                                                                               ^
//   If this fails, ? returns the error from main() immediately.
```

This is how Rust achieves error handling without `try/catch` — errors propagate explicitly up
the call stack.

---

## Concept 3: `Option<T>` for optional config

In the dispatcher's `Config`:

```rust
webhook_url: Option<String>,
email: Option<EmailConfig>,
```

`Option<T>` is either `Some(value)` or `None`. There's no `null` in Rust — if a thing might
not exist, you say so in the type. We later pattern-match on it:

```rust
if let Some(url) = &config.webhook_url {
    // only runs if WEBHOOK_URL was set in the environment
}
```

This forces you at compile time to handle the "not configured" case — no null pointer
exceptions possible.

---

## Concept 4: Ownership and `.clone()` — and when it matters

In `build_smtp_transport` (`dispatcher/src/main.rs`):

```rust
cfg.smtp_user.clone(),
cfg.smtp_pass.clone(),
```

### Why `.clone()` here?

Rust's ownership system says every value has exactly one owner at a time. When a function takes
a `String` argument (not a reference), it takes *ownership* — the caller can no longer use it.
`Credentials::new` wants to own both strings. But `cfg` also owns them, and we might call
`build_smtp_transport` again on the next alarm. So `.clone()` makes a copy — both `cfg` and
`Credentials` own their own version.

### Why "in a hot path you'd think harder about this"

Cloning a `String` heap-allocates a new buffer and copies bytes into it. For two short strings
(username + password) done once per alarm, this is completely negligible — microseconds, tiny
allocation. No concern here.

But imagine a **hot path**: a function called millions of times per second processing a
high-throughput stream, where the strings being cloned are large (e.g., a device's full
JSON payload, or a 10 KB config blob). Now those allocations add up:
- Each clone = malloc + memcpy + eventual free
- Millions/sec × even a few KB = real GC pressure and cache churn
- The allocator becomes a contention point under concurrent load

**What you'd do instead:**

**Option A — borrow (`&str`) rather than own (`String`)**

If the callee only needs to *read* the string, pass a reference:

```rust
fn build_smtp_transport(host: &str, user: &str, pass: &str) -> ... {
    // no clone needed — we're just reading
}
```

Lifetime annotations would ensure the reference can't outlive the data it points to.
The compiler enforces this at zero runtime cost.

**Option B — `Arc<String>` for shared ownership**

If multiple async tasks each need to hold onto the same value concurrently, wrap it in
`Arc` (Atomically Reference Counted):

```rust
let user = Arc::new(cfg.smtp_user.clone()); // clone once at startup
// each task gets a cheap pointer copy, not a string copy
let user_ref = Arc::clone(&user);
```

`Arc::clone` increments a counter — it does *not* copy the string data. When the last
`Arc` is dropped, the string is freed. This is Rust's answer to shared ownership without
a garbage collector.

**Option C — `Cow<'_, str>` (Clone-on-Write)**

For cases where a value is *usually* borrowed but *occasionally* needs to be owned:

```rust
use std::borrow::Cow;
fn process(name: Cow<'_, str>) { ... }
process(Cow::Borrowed("literal")); // zero alloc
process(Cow::Owned(dynamic_string)); // owns when needed
```

### Rule of thumb

- Short-lived, infrequent call + small data → `.clone()` is fine, keep it readable
- Hot path (thousands+/sec) OR large data → reach for `&str`, `Arc`, or `Cow`
- Profile first — don't optimize prematurely

---

## Concept 5: The mock publisher loop trick

```rust
for (i, _) in std::iter::repeat(()).enumerate() {
    let mut event = event_templates().remove(i % count);
    ...
}
```

`std::iter::repeat(())` is an infinite iterator of `()` (unit — Rust's "nothing" value).
`.enumerate()` wraps it to yield `(index, value)` pairs. `i % count` cycles through the
event list forever without needing a `loop { ... }` with manual counter management.
This is idiomatic Rust for "loop forever with a counter."

---

## Running the pipeline

```bash
# Start infrastructure
docker compose -f docker/docker-compose.yml up -d rabbitmq mongodb

# Each in its own terminal:
cargo run -p nervous-system-queue                   # logs events, writes to MongoDB
cargo run -p nervous-system-dispatcher              # sends HTTP/email on alarms
cargo run --bin mock-device                         # fake DeviceEvents → device.events.raw
cargo run --bin mock-alarm                          # fake AlarmDecisions → alarms.dispatch
```
