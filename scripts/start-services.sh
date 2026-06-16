#!/bin/bash
set -e

RUST_DIR="rust"
CPP_BUILD_DIR="cpp/build"

if command -v cmake > /dev/null; then
    echo "Building C++ processing engine..."
    make build-cpp > /dev/null 2>&1
    echo ""
    echo "Starting services in background..."
    echo "   - Rust queue service"
    cargo run --manifest-path "$RUST_DIR"/Cargo.toml -p nervous-system-queue > /tmp/queue.log 2>&1 &

    echo "   - Rust dispatcher (webhook mode)"
    WEBHOOK_URL=http://localhost:8099/alarm cargo run --manifest-path "$RUST_DIR"/Cargo.toml -p nervous-system-dispatcher > /tmp/dispatcher.log 2>&1 &

    echo "   - C++ processing engine"
    AMQP_URI=amqp://guest:guest@localhost:5672 RULES_FILE="$(pwd)"/cpp/processing/config/rules.json "$CPP_BUILD_DIR"/processing/processing_service > /tmp/processing.log 2>&1 &
else
    echo "⚠️  cmake not found — C++ processing engine unavailable (no alarms will fire)"
    echo ""
    echo "Starting Rust services in background..."
    echo "   - Rust queue service"
    cargo run --manifest-path "$RUST_DIR"/Cargo.toml -p nervous-system-queue > /tmp/queue.log 2>&1 &

    echo "   - Rust dispatcher (webhook mode)"
    WEBHOOK_URL=http://localhost:8099/alarm cargo run --manifest-path "$RUST_DIR"/Cargo.toml -p nervous-system-dispatcher > /tmp/dispatcher.log 2>&1 &
fi

sleep 2
echo ""
echo "✅ Pipeline services running in background."
echo ""
echo "Next steps:"
echo "  make webhook-listener   # (another terminal) listen for alarm notifications"
echo "  make stop               # shut down all services"
echo ""
echo "Inject a single event:"
echo "  cargo run --manifest-path rust/Cargo.toml -p demo-cli -- --event smoke_detected --count 1"
