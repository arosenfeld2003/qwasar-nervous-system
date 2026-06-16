cmake_minimum_required(VERSION 3.20)
project(nervous_system CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# rabbitmq-c — synchronous AMQP client used by AmqpRabbitMQPublisher
FetchContent_Declare(
    rabbitmq-c
    GIT_REPOSITORY https://github.com/alanxz/rabbitmq-c.git
    GIT_TAG v0.13.0
)
set(BUILD_TOOLS     OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(ENABLE_SSL_SUPPORT OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rabbitmq-c)

# Paho MQTT is optional — only needed for protocol adapters, not the processing engine
find_package(PahoMqttCpp QUIET)

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.12.6
)
FetchContent_MakeAvailable(httplib)

# Only build adapters if Paho MQTT is available
if (PahoMqttCpp_FOUND)
    add_subdirectory(adapters)
endif()

# Always build processing engine (uses only rabbitmq-c)
add_subdirectory(processing)

