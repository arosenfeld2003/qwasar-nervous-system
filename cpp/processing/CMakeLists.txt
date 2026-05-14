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

# Optional: Real RabbitMQ publisher with amqp-cpp (requires amqp-cpp library)
# Uncomment when adding actual RabbitMQ integration
# add_library(rabbitmq_engine STATIC
#     src/rabbitmq_engine.cpp
# )
# target_link_libraries(rabbitmq_engine PUBLIC processing_engine PRIVATE amqpcpp)

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
