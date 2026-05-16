#include "amqp_publisher.hpp"
#include "processing_engine_impl.hpp"
#include "rules_loader.hpp"
#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

// volatile sig_atomic_t is the only type guaranteed safe to read/write from a
// signal handler.  g_running is set to 0 by handle_signal() and checked by the
// main loop — this is the standard graceful-shutdown pattern for POSIX services.
static volatile sig_atomic_t g_running = 1;

static void handle_signal(int) { g_running = 0; }

static std::string env_or(const char* name, const char* fallback) {
    const char* val = std::getenv(name);
    return val ? val : fallback;
}

// Mirrors check_amqp_reply in amqp_publisher.cpp.  Defined separately here
// because the consumer connection is a raw amqp_connection_state_t managed
// directly in main(), not through AmqpRabbitMQPublisher.
static void check_reply(amqp_rpc_reply_t r, const char* ctx) {
    if (r.reply_type == AMQP_RESPONSE_NORMAL) return;
    if (r.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
        throw std::runtime_error(std::string(ctx) + ": " + amqp_error_string2(r.library_error));
    }
    throw std::runtime_error(std::string(ctx) + ": unexpected broker response");
}

int main() {
    // Register both SIGINT (Ctrl-C) and SIGTERM (docker stop / systemd) so the
    // service flushes in-flight acks before exiting in all common scenarios.
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    const std::string amqp_uri   = env_or("AMQP_URI",        "amqp://guest:guest@localhost:5672");
    const std::string rules_file = env_or("RULES_FILE",      "config/rules.json");
    const std::string inbound    = env_or("INBOUND_QUEUE",    "device.events.normalized");

    // ── Engine setup ─────────────────────────────────────────────────────────
    // Rules are loaded once at startup.  A restart is required to pick up rule
    // changes — keeping the hot path free of file I/O or mutex contention.
    std::cout << "[processing-service] loading rules from " << rules_file << "\n";
    auto rules = nervous_system::RulesLoader::load_from_json_file(rules_file);
    std::cout << "[processing-service] loaded " << rules.size() << " rule(s)\n";

    // The publisher gets its own AMQP connection managed by AmqpRabbitMQPublisher.
    // Keeping the producer and consumer on separate connections prevents the
    // consumer's prefetch window from blocking the publisher's channel.
    auto publisher = std::make_shared<nervous_system::AmqpRabbitMQPublisher>(amqp_uri);
    nervous_system::ProcessingEngineImpl engine(publisher);
    engine.load_rules(rules);

    // ── Consumer connection ───────────────────────────────────────────────────
    // A second, independent AMQP connection is opened here for consuming
    // device.events.normalized.  Using a separate connection from the publisher
    // avoids channel-level flow control on one side from stalling the other.
    std::cout << "[processing-service] connecting consumer to " << amqp_uri << "\n";

    amqp_connection_info info;
    // amqp_parse_url writes into url_buf in-place; info pointers reference that
    // buffer, so url_buf must remain alive for the lifetime of `info`.
    std::string url_buf = amqp_uri;
    if (amqp_parse_url(url_buf.data(), &info) != AMQP_STATUS_OK) {
        std::cerr << "[processing-service] invalid AMQP URI\n";
        return 1;
    }

    amqp_connection_state_t conn = amqp_new_connection();
    amqp_socket_t* socket = amqp_tcp_socket_new(conn);
    if (amqp_socket_open(socket, info.host, info.port) != AMQP_STATUS_OK) {
        std::cerr << "[processing-service] failed to connect\n";
        return 1;
    }

    check_reply(amqp_login(conn, info.vhost, 0, 131072, 0,
                           AMQP_SASL_METHOD_PLAIN, info.user, info.password), "login");
    amqp_channel_open(conn, 1);
    check_reply(amqp_get_rpc_reply(conn), "channel open");

    // Declare inbound queue with durable=1 so it survives a broker restart and
    // messages queued while the processing service is down are not lost.
    amqp_queue_declare(conn, 1, amqp_cstring_bytes(inbound.c_str()),
                       0, 1, 0, 0, amqp_empty_table);
    check_reply(amqp_get_rpc_reply(conn), "queue declare");

    // Prefetch 16 messages at a time.  This limits the number of unacknowledged
    // messages the broker delivers before waiting for acks, bounding memory
    // usage under a burst of device events without starving throughput.
    amqp_basic_qos(conn, 1, 0, 16, 0);
    amqp_get_rpc_reply(conn);

    // no_ack=0: messages require explicit ack/nack.  This is critical — without
    // explicit acks, a crashed processing service causes the broker to silently
    // drop all in-flight alarm evaluations.
    amqp_basic_consume(conn, 1, amqp_cstring_bytes(inbound.c_str()),
                       amqp_empty_bytes, /*no_local=*/0, /*no_ack=*/0,
                       /*exclusive=*/0, amqp_empty_table);
    check_reply(amqp_get_rpc_reply(conn), "basic consume");

    std::cout << "[processing-service] consuming from '" << inbound << "' — Ctrl-C to stop\n";

    // ── Event loop ────────────────────────────────────────────────────────────
    while (g_running) {
        amqp_rpc_reply_t res;
        amqp_envelope_t envelope;

        // Release frame buffers from the previous iteration back to the
        // rabbitmq-c memory pool.  Without this call, each iteration leaks the
        // envelope body allocation — critical in a long-running service.
        amqp_maybe_release_buffers(conn);

        // 1-second timeout allows the signal handler to set g_running=0 and
        // have the loop exit within one second, rather than blocking forever on
        // an idle queue.
        struct timeval timeout { .tv_sec = 1, .tv_usec = 0 };
        res = amqp_consume_message(conn, &envelope, &timeout, 0);

        if (res.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION &&
            res.library_error == AMQP_STATUS_TIMEOUT) {
            continue;  // no message arrived; re-check g_running
        }
        if (res.reply_type != AMQP_RESPONSE_NORMAL) {
            std::cerr << "[processing-service] consumer error, shutting down\n";
            break;
        }

        std::string body(static_cast<char*>(envelope.message.body.bytes),
                         envelope.message.body.len);

        try {
            auto event = nlohmann::json::parse(body);
            engine.evaluate(event, [](const nervous_system::AlarmDecision& alarm) {
                std::cout << "[processing-service] alarm triggered: "
                          << alarm.alarm_type << " / " << alarm.severity
                          << " on " << alarm.device_id << "\n";
            });
            // Ack only after evaluate() succeeds so that a crash mid-evaluation
            // causes the broker to redeliver the message on the next connection.
            amqp_basic_ack(conn, 1, envelope.delivery_tag, /*multiple=*/0);
        } catch (const std::exception& e) {
            std::cerr << "[processing-service] failed to process message: " << e.what() << "\n";
            // requeue=0: send the message to the dead-letter exchange (if
            // configured) instead of requeueing, to prevent a malformed event
            // from looping forever and blocking healthy events behind it.
            amqp_basic_nack(conn, 1, envelope.delivery_tag, /*multiple=*/0, /*requeue=*/0);
        }

        amqp_destroy_envelope(&envelope);
    }

    std::cout << "[processing-service] shutting down\n";
    amqp_channel_close(conn, 1, AMQP_REPLY_SUCCESS);
    amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn);

    return 0;
}
