.PHONY: dev dev-down dev-logs build-rust test-rust check-rust fmt-rust build-cpp

RUST_DIR := rust
CPP_BUILD_DIR := cpp/build

# ── Local dev services ────────────────────────────────────────────────────
dev:
	docker compose -f docker/docker-compose.yml up -d
	@echo ""
	@echo "  RabbitMQ management: http://localhost:15672  (guest/guest)"
	@echo "  Temporal UI:         http://localhost:8088"
	@echo "  MongoDB:             localhost:27017"
	@echo "  Redis:               localhost:6379"

dev-down:
	docker compose -f docker/docker-compose.yml down

dev-logs:
	docker compose -f docker/docker-compose.yml logs -f

# ── Rust ──────────────────────────────────────────────────────────────────
build-rust:
	cargo build --manifest-path $(RUST_DIR)/Cargo.toml

test-rust:
	cargo test --manifest-path $(RUST_DIR)/Cargo.toml

check-rust:
	cargo check --manifest-path $(RUST_DIR)/Cargo.toml

fmt-rust:
	cargo fmt --manifest-path $(RUST_DIR)/Cargo.toml

# ── C++ ───────────────────────────────────────────────────────────────────
build-cpp:
	cmake -S cpp -B $(CPP_BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	cmake --build $(CPP_BUILD_DIR) --parallel

clean-cpp:
	rm -rf $(CPP_BUILD_DIR)
