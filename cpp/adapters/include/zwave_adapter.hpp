#pragma once

#include "adapter.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace nervous_system {

class ZWaveAdapter : public IAdapter {
public:
    explicit ZWaveAdapter(std::chrono::seconds interval = std::chrono::seconds(10));
    ~ZWaveAdapter() override;

    void start(EventCallback on_event) override;
    void stop() override;
    std::string protocol_name() const override { return "zwave"; }

private:
    std::chrono::seconds interval_;
    std::atomic<bool> running_ = false;
    std::thread worker_;
    std::mutex callback_mutex_;
    std::mutex worker_mutex_;
    std::condition_variable worker_cv_;
    EventCallback on_event_;
    std::unique_ptr<class RabbitMQClient> rabbitmq_client_;

    void run_loop();
    void publish_device_event(const DeviceEvent& event);
};

}  // namespace nervous_system
