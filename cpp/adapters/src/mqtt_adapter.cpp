#include "mqtt_adapter.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "mqtt/async_client.h"

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

std::string make_client_id() {
    return "nervous-system-mqtt-adapter-" + std::to_string(current_timestamp_ms());
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

class MqttAdapter::Callback : public virtual mqtt::callback {
public:
    explicit Callback(MqttAdapter& owner) : owner_(owner) {}

    void connection_lost(const std::string& cause) override {
        std::cerr << "MQTT connection lost: " << cause << "\n";
    }

    void message_arrived(mqtt::const_message_ptr msg) override {
        owner_.handle_mqtt_message(msg->get_topic(), msg->to_string());
    }

    void delivery_complete(mqtt::delivery_token_ptr) override {}

private:
    MqttAdapter& owner_;
};

}  // namespace

MqttAdapter::MqttAdapter(std::string broker_url, std::string topic)
    : broker_url_(std::move(broker_url)),
      topic_(std::move(topic)),
      client_id_(make_client_id()),
      callback_(std::make_unique<Callback>(*this)) {
}

MqttAdapter::~MqttAdapter() {
    stop();
}

void MqttAdapter::start(EventCallback on_event) {
    std::lock_guard lock(callback_mutex_);
    if (running_.load()) {
        return;
    }

    on_event_ = std::move(on_event);

    try {
        client_ = std::make_unique<mqtt::async_client>(broker_url_, client_id_);
        client_->set_callback(*callback_);

        mqtt::connect_options connect_opts;
        connect_opts.set_clean_session(true);

        client_->connect(connect_opts)->wait();
        client_->subscribe(topic_, 1)->wait();

        rabbitmq_client_ = std::make_unique<RabbitMQClient>(rabbitmq_url_from_env());
        running_.store(true);
    } catch (const mqtt::exception& exc) {
        throw std::runtime_error(std::string("MQTT adapter start failed: ") + exc.what());
    }
}

void MqttAdapter::stop() {
    std::lock_guard lock(callback_mutex_);
    if (!running_.load()) {
        return;
    }

    if (client_) {
        try {
            if (client_->is_connected()) {
                client_->disconnect()->wait();
            }
        } catch (const mqtt::exception& exc) {
            std::cerr << "MQTT disconnect failed: " << exc.what() << "\n";
        }
        client_.reset();
    }

    rabbitmq_client_.reset();
    running_.store(false);
}

void MqttAdapter::handle_mqtt_message(const std::string& topic, const std::string& payload) {
    nlohmann::json message_json;
    try {
        message_json = nlohmann::json::parse(payload);
    } catch (const std::exception&) {
        message_json = nlohmann::json::object({{"raw", payload}});
    }

    DeviceEvent event;
    event.device_id = message_json.value("device_id", topic);
    event.protocol = protocol_name();
    event.event_type = message_json.value("event_type", "mqtt_message");
    event.timestamp_ms = message_json.value("timestamp_ms", current_timestamp_ms());

    if (message_json.contains("payload") && message_json["payload"].is_object()) {
        event.payload = message_json["payload"];
    } else if (message_json.is_object()) {
        nlohmann::json payload_object = message_json;
        payload_object.erase("device_id");
        payload_object.erase("protocol");
        payload_object.erase("event_type");
        payload_object.erase("timestamp_ms");
        payload_object.erase("payload");

        if (payload_object.empty()) {
            event.payload = { {"raw", payload} };
        } else {
            event.payload = std::move(payload_object);
        }
    } else {
        event.payload = { {"raw", payload} };
    }

    EventCallback callback;
    {
        std::lock_guard lock(callback_mutex_);
        callback = on_event_;
    }

    if (callback) {
        callback(event);
    }

    publish_device_event(event);
}

void MqttAdapter::publish_device_event(const DeviceEvent& event) {
    if (rabbitmq_client_) {
        rabbitmq_client_->publish("device.events.raw", event.to_json());
    }
}

}  // namespace nervous_system
