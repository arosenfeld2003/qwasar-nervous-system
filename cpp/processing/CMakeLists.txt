add_library(processing INTERFACE)

target_include_directories(processing INTERFACE include)
target_link_libraries(processing INTERFACE nlohmann_json::nlohmann_json)

# Processing Engine Implementation
add_library(processing_engine STATIC
    src/processing_engine.cpp
    src/rabbitmq_publisher.cpp
)

target_link_libraries(processing_engine PUBLIC processing)
target_include_directories(processing_engine PUBLIC include)

# Real AMQP publisher using rabbitmq-c
add_library(amqp_publisher STATIC
    src/amqp_publisher.cpp
)
target_link_libraries(amqp_publisher PUBLIC processing_engine PRIVATE rabbitmq)
target_include_directories(amqp_publisher PUBLIC include)

# Processing service — consumes device.events.normalized and publishes alarms
add_executable(processing_service
    src/main.cpp
)
target_link_libraries(processing_service PRIVATE amqp_publisher processing_engine rabbitmq)

# Unit tests
enable_testing()
add_executable(test_processing_engine
    tests/test_processing_engine.cpp
)
target_link_libraries(test_processing_engine PRIVATE processing_engine)

# GoogleTest integration (optional)
# find_package(GTest REQUIRED)
# target_link_libraries(test_processing_engine PRIVATE GTest::gtest GTest::gtest_main)

add_test(NAME ProcessingEngineTests COMMAND test_processing_engine)
