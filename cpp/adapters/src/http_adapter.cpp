#include "http_adapter.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

#include <httplib.h>

#ifdef HAS_RABBITMQ_C
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#endif

namespace nervous_system {

namespace {

uint64_t current_timestamp_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string rabbitmq_url_from_env() {
    const char* env = std::getenv("RABBITMQ_URL");
    return env ? std::string(env) : std::string("amqp://guest:guest@localhost:5672/");
}

#ifdef HAS_RABBITMQ_C
class RabbitMQClient {
public:
    explicit RabbitMQClient(const std::string& uri)
        : connection_(amqp_new_connection()), channel_id_(1) {
        amqp_connection_info_t info;
        if (amqp_parse_url(uri.c_str(), &info) != AMQP_STATUS_OK) {
            throw std::runtime_error("RabbitMQ URL parse failed: " + uri);
        }

        std::string host(reinterpret_cast<const char*>(info.host.bytes), info.host.len);
        std::string user(reinterpret_cast<const char*>(info.user.bytes), info.user.len);
        std::string password(reinterpret_cast<const char*>(info.password.bytes), info.password.len);
        std::string vhost(reinterpret_cast<const char*>(info.vhost.bytes), info.vhost.len);

        amqp_socket_t* socket = amqp_tcp_socket_new(connection_);
        if (!socket) {
            throw std::runtime_error("Failed to create RabbitMQ TCP socket");
        }

        int status = amqp_socket_open(socket, host.c_str(), info.port);
        if (status != AMQP_STATUS_OK) {
            throw std::runtime_error("Failed to open RabbitMQ socket: " + std::to_string(status));
        }

        amqp_rpc_reply_t login_reply = amqp_login(connection_,
            vhost.c_str(),
            0, 131072, 0, AMQP_SASL_METHOD_PLAIN,
            user.c_str(), password.c_str());
        if (login_reply.reply_type != AMQP_RESPONSE_NORMAL) {
            throw std::runtime_error("RabbitMQ login failed");
        }

        amqp_channel_open(connection_, channel_id_);
        if (amqp_get_rpc_reply(connection_).reply_type != AMQP_RESPONSE_NORMAL) {
            throw std::runtime_error("Failed to open RabbitMQ channel");
        }
    }

    ~RabbitMQClient() {
        if (connection_) {
            amqp_channel_close(connection_, channel_id_, AMQP_REPLY_SUCCESS);
            amqp_connection_close(connection_, AMQP_REPLY_SUCCESS);
            amqp_destroy_connection(connection_);
        }
    }

    void publish(const std::string& queue, const nlohmann::json& message) {
        declare_queue(queue);

        amqp_basic_properties_t props;
        props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
        props.content_type = amqp_cstring_bytes("application/json");
        props.delivery_mode = 2;

        std::string payload = message.dump();
        int status = amqp_basic_publish(connection_, channel_id_,
            amqp_cstring_bytes(""),
            amqp_cstring_bytes(queue.c_str()),
            0, 0, &props,
            amqp_cstring_bytes(payload.c_str()));

        if (status != AMQP_STATUS_OK) {
            std::cerr << "Failed to publish RabbitMQ message: " << status << "\n";
        }

        amqp_maybe_release_buffers(connection_);
    }

private:
    void declare_queue(const std::string& queue) {
        amqp_queue_declare_ok_t* result = amqp_queue_declare(connection_, channel_id_,
            amqp_cstring_bytes(queue.c_str()),
            0, 1, 0, 0, amqp_empty_table);
        if (amqp_get_rpc_reply(connection_).reply_type != AMQP_RESPONSE_NORMAL || !result) {
            throw std::runtime_error("Failed to declare RabbitMQ queue: " + queue);
        }
    }

    amqp_connection_state_t connection_;
    int channel_id_;
};
#else
class RabbitMQClient {
public:
    explicit RabbitMQClient(const std::string&) {}
    void publish(const std::string&, const nlohmann::json&) {}
};
#endif

}  // namespace

HttpAdapter::HttpAdapter(int port) : port_(port) {}

HttpAdapter::~HttpAdapter() {
    stop();
}

void HttpAdapter::start(EventCallback on_event) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (running_.load()) {
        return;
    }

    on_event_ = std::move(on_event);
    rabbitmq_client_ = std::make_unique<RabbitMQClient>(rabbitmq_url_from_env());
    server_ = std::make_unique<httplib::Server>();

    server_->Post("/events", [this](const httplib::Request& req, httplib::Response& res) {
        handle_post_events(req, res);
    });

    running_.store(true);
    server_thread_ = std::thread([this] {
        if (!server_->listen("0.0.0.0", port_)) {
            std::cerr << "HTTP adapter failed to bind port " << port_ << "\n";
        }
    });
}

void HttpAdapter::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);
    if (server_) {
        server_->stop();
    }

    if (server_thread_.joinable()) {
        server_thread_.join();
    }

    rabbitmq_client_.reset();
    server_.reset();
}

void HttpAdapter::handle_post_events(const httplib::Request& req, httplib::Response& res) {
    if (req.body.empty()) {
        res.status = 400;
        res.set_content("empty request body", "text/plain");
        return;
    }

    nlohmann::json body_json;
    try {
        body_json = nlohmann::json::parse(req.body);
    } catch (const std::exception&) {
        res.status = 400;
        res.set_content("invalid JSON", "text/plain");
        return;
    }

    if (!body_json.is_object()) {
        res.status = 400;
        res.set_content("expected JSON object", "text/plain");
        return;
    }

    try {
        if (!body_json.contains("device_id") || !body_json["device_id"].is_string() ||
            !body_json.contains("protocol") || !body_json["protocol"].is_string() ||
            !body_json.contains("event_type") || !body_json["event_type"].is_string() ||
            !body_json.contains("timestamp_ms") || !body_json["timestamp_ms"].is_number_unsigned() ||
            !body_json.contains("payload") || !body_json["payload"].is_object()) {
            throw std::runtime_error("invalid schema");
        }

        std::string protocol = body_json["protocol"].get<std::string>();
        if (protocol != protocol_name()) {
            throw std::runtime_error("protocol must be http");
        }

        DeviceEvent event;
        event.device_id = body_json["device_id"].get<std::string>();
        event.protocol = protocol_name();
        event.event_type = body_json["event_type"].get<std::string>();
        event.timestamp_ms = body_json["timestamp_ms"].get<uint64_t>();
        event.payload = body_json["payload"];

        EventCallback callback;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback = on_event_;
        }

        if (callback) {
            callback(event);
        }

        publish_device_event(event);
        res.status = 200;
        res.set_content("ok", "text/plain");
    } catch (const std::exception& ex) {
        res.status = 400;
        res.set_content(std::string("bad request: ") + ex.what(), "text/plain");
    }
}

void HttpAdapter::publish_device_event(const DeviceEvent& event) {
    if (rabbitmq_client_) {
        rabbitmq_client_->publish("device.events.raw", event.to_json());
    }
}

}  // namespace nervous_system
