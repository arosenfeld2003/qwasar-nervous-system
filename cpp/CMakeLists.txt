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

find_package(PahoMqttCpp QUIET)
if (NOT PahoMqttCpp_FOUND)
    FetchContent_Declare(
        paho_mqtt_c
        GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.c.git
        GIT_TAG v1.4.0
    )
    FetchContent_MakeAvailable(paho_mqtt_c)

    FetchContent_Declare(
        paho_mqtt_cpp
        GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.cpp.git
        GIT_TAG v1.3.1
    )
    FetchContent_MakeAvailable(paho_mqtt_cpp)
endif()

FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.12.6
)
FetchContent_MakeAvailable(httplib)

add_subdirectory(adapters)
add_subdirectory(processing)

