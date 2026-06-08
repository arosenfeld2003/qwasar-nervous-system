#pragma once

#include "adapter.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace httplib {
class Server;
class Request;
class Response;
}

namespace nervous_system {

class HttpAdapter : public IAdapter {
public:
    explicit HttpAdapter(int port = 8090);
    ~HttpAdapter() override;

    void start(EventCallback on_event) override;
    void stop() override;
    std::string protocol_name() const override { return "http"; }

private:
    int port_;
    std::atomic<bool> running_ = false;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
    std::mutex callback_mutex_;
    EventCallback on_event_;
    std::unique_ptr<class RabbitMQClient> rabbitmq_client_;

    void handle_post_events(const httplib::Request& req, httplib::Response& res);
    void publish_device_event(const DeviceEvent& event);
};

}  // namespace nervous_system
