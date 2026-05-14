#include "amqp_publisher.hpp"
#include <amqp.h>
#include <amqp_tcp_socket.h>
#include <cstring>
#include <stdexcept>
#include <unordered_set>

namespace nervous_system {

struct AmqpRabbitMQPublisher::Impl {
    amqp_connection_state_t conn = nullptr;
    std::unordered_set<std::string> declared_queues;
};

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
        amqp_channel_close(impl_->conn, 1, AMQP_REPLY_SUCCESS);
        amqp_connection_close(impl_->conn, AMQP_REPLY_SUCCESS);
        amqp_destroy_connection(impl_->conn);
    }
    delete impl_;
}

void AmqpRabbitMQPublisher::connect(const std::string& uri) {
    amqp_connection_info info;
    // amqp_parse_url modifies the string in-place
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

    check_amqp_reply(
        amqp_login(impl_->conn, info.vhost, 0, 131072, 0,
                   AMQP_SASL_METHOD_PLAIN, info.user, info.password),
        "login");

    amqp_channel_open(impl_->conn, 1);
    check_amqp_reply(amqp_get_rpc_reply(impl_->conn), "channel open");
}

void AmqpRabbitMQPublisher::declare_queue(const std::string& queue) {
    if (impl_->declared_queues.count(queue)) return;
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
    props.delivery_mode = 2;  // persistent

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
