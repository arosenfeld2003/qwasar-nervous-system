#include "amqp_publisher.hpp"
#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace nervous_system {

struct AmqpRabbitMQPublisher::Impl {
    amqp_connection_state_t conn = nullptr;
    // Tracks which queues have already been declared on this connection so
    // declare_queue() avoids a redundant broker round-trip per publish call.
    std::unordered_set<std::string> declared_queues;
};

// rabbitmq-c uses a plain integer reply type, not exceptions.  This helper
// converts every non-normal reply into a std::runtime_error so callers can use
// RAII without sprinkling reply-type checks everywhere.
static void check_amqp_reply(amqp_rpc_reply_t reply, const char* context) {
    if (reply.reply_type == AMQP_RESPONSE_NORMAL) return;
    if (reply.reply_type == AMQP_RESPONSE_LIBRARY_EXCEPTION) {
        throw std::runtime_error(std::string(context) + ": " + amqp_error_string2(reply.library_error));
    }
    throw std::runtime_error(std::string(context) + ": unexpected broker response");
}

AmqpRabbitMQPublisher::AmqpRabbitMQPublisher(const std::string& amqp_uri)
    : impl_(new Impl()) {
    connect(amqp_uri);
}

AmqpRabbitMQPublisher::~AmqpRabbitMQPublisher() {
    if (impl_->conn) {
        // AMQP protocol requires channel close before connection close.
        // Skipping the channel close frame causes the broker to log a warning
        // and may delay connection teardown on heavily loaded brokers.
        amqp_channel_close(impl_->conn, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(impl_->conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(impl_->conn);
    }
    delete impl_;
}

void AmqpRabbitMQPublisher::connect(const std::string& uri) {
    amqp_connection_info info;
    // amqp_parse_url writes null terminators into the buffer it parses, so
    // info.host / info.user / info.password point into url_buf's storage.
    // url_buf must outlive all uses of `info`.
    std::string url_buf = uri;
    if (amqp_parse_url(url_buf.data(), &info) != AMQP_STATUS_OK) {
        throw std::runtime_error("invalid AMQP URI: " + uri);
    }

    impl_->conn = amqp_new_connection();
    amqp_socket_t* socket = amqp_tcp_socket_new(impl_->conn);
    if (!socket) throw std::runtime_error("failed to create TCP socket");

    if (amqp_socket_open(socket, info.host, info.port) != AMQP_STATUS_OK) {
        throw std::runtime_error(std::string("failed to connect to ") + info.host);
    }

    // amqp_login arguments:
    //   channel_max = 0  → let the broker negotiate (typically 2047)
    //   frame_max   = 131072 (128 KiB) → matches RabbitMQ's default; alarm
    //                 JSON payloads are tiny, so this is not a bottleneck
    //   heartbeat   = 0  → disabled; the processing service runs in a tight
    //                 loop and sends frames continuously, making heartbeats
    //                 unnecessary overhead
    check_amqp_reply(
        amqp_login(impl_->conn, info.vhost, 0, 131072, 0,
                   AMQP_SASL_METHOD_PLAIN, info.user, info.password),
        "login");

    // Channel 1 is the conventional first channel.  rabbitmq-c is not
    // multiplexed in practice here — one publisher, one channel is sufficient.
    amqp_channel_open(impl_->conn, 1);
    check_amqp_reply(amqp_get_rpc_reply(impl_->conn), "channel open");
}

void AmqpRabbitMQPublisher::declare_queue(const std::string& queue) {
    if (impl_->declared_queues.count(queue)) return;
    // passive=0: create the queue if it doesn't exist.
    // durable=1: the queue survives a broker restart; critical for alarm
    //            decisions that must not be lost if the broker is bounced.
    // exclusive=0, auto_delete=0: the queue persists independently of this
    //            connection so a restarted processing service picks up where it
    //            left off.
    amqp_queue_declare(impl_->conn, 1,
                       amqp_cstring_bytes(queue.c_str()),
                       /*passive=*/0, /*durable=*/1,
                       /*exclusive=*/0, /*auto_delete=*/0,
                       amqp_empty_table);
    check_amqp_reply(amqp_get_rpc_reply(impl_->conn), "queue declare");
    impl_->declared_queues.insert(queue);
}

void AmqpRabbitMQPublisher::publish(const std::string& queue, const nlohmann::json& message) {
    declare_queue(queue);

    std::string body = message.dump();

    amqp_basic_properties_t props;
    props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
    props.content_type = amqp_cstring_bytes("application/json");
    // delivery_mode=2 marks the message persistent: the broker writes it to
    // disk before acknowledging, so an alarm decision survives a broker crash.
    props.delivery_mode = 2;

    // Publishing to the default exchange ("") routes by routing key = queue
    // name, which is the simplest point-to-point model.  mandatory=0 means the
    // broker silently drops unroutable messages rather than returning them;
    // acceptable here because declare_queue() guarantees the queue exists.
    int rc = amqp_basic_publish(
        impl_->conn, 1,
        amqp_cstring_bytes(""),          // default exchange
        amqp_cstring_bytes(queue.c_str()),
        /*mandatory=*/0, /*immediate=*/0,
        &props,
        amqp_cstring_bytes(body.c_str()));

    if (rc != AMQP_STATUS_OK) {
        throw std::runtime_error(std::string("publish failed: ") + amqp_error_string2(rc));
    }
}

}  // namespace nervous_system
