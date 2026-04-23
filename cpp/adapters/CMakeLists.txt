add_library(adapters INTERFACE)

target_include_directories(adapters INTERFACE include)
target_link_libraries(adapters INTERFACE nlohmann_json::nlohmann_json)

# Tim: add adapter implementations here, e.g.:
# add_library(mqtt_adapter STATIC src/mqtt_adapter.cpp)
# target_link_libraries(mqtt_adapter PUBLIC adapters PahoMqttCpp::PahoMqttCpp)
