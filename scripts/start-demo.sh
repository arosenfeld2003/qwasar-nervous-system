#!/bin/bash
set -e

RUST_DIR="rust"
CPP_BUILD_DIR="cpp/build"

echo "2. Building C++ processing engine (if cmake available)..."
if command -v cmake > /dev/null; then
    make build-cpp > /dev/null 2>&1
    echo ""
    echo "3. Starting services in background..."
    echo "   - Rust queue service"
    cargo run --manifest-path "$RUST_DIR"/Cargo.toml -p nervous-system-queue > /tmp/queue.log 2>&1 &

    echo "   - Rust dispatcher (webhook mode)"
    WEBHOOK_URL=http://localhost:8099/alarm cargo run --manifest-path "$RUST_DIR"/Cargo.toml -p nervous-system-dispatcher > /tmp/dispatcher.log 2>&1 &

    echo "   - C++ processing engine"
    AMQP_URI=amqp://guest:guest@localhost:5672 RULES_FILE="$(pwd)"/cpp/processing/config/rules.json "$CPP_BUILD_DIR"/processing/processing_service > /tmp/processing.log 2>&1 &
else
    echo "⚠️  cmake not found, skipping C++ processing engine. Running demo with Rust components only."
    echo ""
    echo "3. Starting services in background..."
    echo "   - Rust queue service"
    cargo run --manifest-path "$RUST_DIR"/Cargo.toml -p nervous-system-queue > /tmp/queue.log 2>&1 &

    echo "   - Rust dispatcher (webhook mode)"
    WEBHOOK_URL=http://localhost:8099/alarm cargo run --manifest-path "$RUST_DIR"/Cargo.toml -p nervous-system-dispatcher > /tmp/dispatcher.log 2>&1 &
fi

sleep 2
echo ""
echo "4. 📡 In another terminal, run: make webhook-listener"
echo ""
echo "5. Launching demo CLI..."
echo ""

cargo run --manifest-path "$RUST_DIR"/Cargo.toml -p demo-cli -- --interval-ms 1500
