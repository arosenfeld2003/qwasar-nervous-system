#include "zwave_adapter.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

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

std::string make_device_id() {
    return "zwave-door-sensor-1";
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

ZWaveAdapter::ZWaveAdapter(std::chrono::seconds interval)
    : interval_(interval) {}

ZWaveAdapter::~ZWaveAdapter() {
    stop();
}

void ZWaveAdapter::start(EventCallback on_event) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (running_.load()) {
        return;
    }

    on_event_ = std::move(on_event);
    rabbitmq_client_ = std::make_unique<RabbitMQClient>(rabbitmq_url_from_env());
    running_.store(true);
    worker_ = std::thread([this] { run_loop(); });
}

void ZWaveAdapter::stop() {
    {
        std::lock_guard<std::mutex> lock(worker_mutex_);
        if (!running_.load()) {
            return;
        }
        running_.store(false);
    }

    worker_cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }

    rabbitmq_client_.reset();
}

void ZWaveAdapter::run_loop() {
    std::unique_lock<std::mutex> lock(worker_mutex_);

    while (running_.load()) {
        if (worker_cv_.wait_for(lock, interval_, [this] { return !running_.load(); })) {
            break;
        }

        if (!running_.load()) {
            break;
        }

        DeviceEvent event;
        event.device_id = make_device_id();
        event.protocol = protocol_name();
        event.event_type = "door_open";
        event.timestamp_ms = current_timestamp_ms();
        event.payload = {
            {"open", true},
            {"signal_strength", -52},
            {"battery", 88}
        };

        EventCallback callback;
        {
            std::lock_guard<std::mutex> callback_lock(callback_mutex_);
            callback = on_event_;
        }

        if (callback) {
            callback(event);
        }

        publish_device_event(event);
    }
}

void ZWaveAdapter::publish_device_event(const DeviceEvent& event) {
    if (rabbitmq_client_) {
        rabbitmq_client_->publish("device.events.raw", event.to_json());
    }
}

}  // namespace nervous_system
