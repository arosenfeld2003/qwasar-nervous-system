#pragma once

#include "processing_engine_impl.hpp"
#include <string>

namespace nervous_system {

// Concrete RabbitMQ publisher built on the rabbitmq-c synchronous C library.
//
// Design constraints:
//   - rabbitmq-c uses a single-threaded, blocking I/O model.  There is no
//     async dispatch; each publish() call round-trips through the socket.
//   - One connection, one channel (channel 1) per instance.  If the service
//     needs concurrent publishing it should run separate instances, each on
//     its own thread.
//   - Not thread-safe.  The caller (processing_service main loop) is
//     single-threaded, which makes this safe without locking.
class AmqpRabbitMQPublisher : public RabbitMQPublisher {
public:
    // Parses amqp_uri (e.g. "amqp://user:pass@host:5672/vhost"), opens a TCP
    // socket, logs in, and opens channel 1.  Throws std::runtime_error on any
    // failure so the service fails fast at startup rather than silently dropping
    // alarms later.
    explicit AmqpRabbitMQPublisher(const std::string& amqp_uri);

    // Gracefully closes channel 1, then the connection, before freeing the
    // rabbitmq-c state.  The ordering matters: the broker expects the channel
    // close frame before the connection close frame.
    ~AmqpRabbitMQPublisher() override;

    // Serialises message to JSON, declares the target queue on first use
    // (idempotent), then publishes with delivery_mode=2 (persistent) so the
    // broker survives a restart without losing in-flight alarm decisions.
    void publish(const std::string& queue, const nlohmann::json& message) override;

private:
    // Pimpl keeps rabbitmq-c headers (amqp.h) out of this header.  That avoids
    // exposing the C library's global type aliases to every translation unit
    // that includes amqp_publisher.hpp.
    struct Impl;
    Impl* impl_;

    void connect(const std::string& uri);

    // Sends a queue.declare to the broker the first time a queue name is seen,
    // then caches it in impl_->declared_queues.  Re-declaring an existing
    // durable queue with the same parameters is a no-op on the broker side, so
    // the caching is purely for reducing unnecessary round-trips.
    void declare_queue(const std::string& queue);
};

}  // namespace nervous_system
