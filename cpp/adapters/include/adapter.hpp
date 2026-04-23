#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>

namespace nervous_system {

struct DeviceEvent {
    std::string device_id;
    std::string protocol;
    std::string event_type;
    uint64_t timestamp_ms;
    nlohmann::json payload;

    nlohmann::json to_json() const {
        return {
            {"device_id", device_id},
            {"protocol", protocol},
            {"event_type", event_type},
            {"timestamp_ms", timestamp_ms},
            {"payload", payload},
        };
    }
};

using EventCallback = std::function<void(DeviceEvent)>;

class IAdapter {
public:
    virtual ~IAdapter() = default;

    virtual void start(EventCallback on_event) = 0;
    virtual void stop() = 0;
    virtual std::string protocol_name() const = 0;
};

}  // namespace nervous_system
