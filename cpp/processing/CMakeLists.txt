add_library(processing INTERFACE)

target_include_directories(processing INTERFACE include)
target_link_libraries(processing INTERFACE nlohmann_json::nlohmann_json)

# Tim: add processing engine implementation here, e.g.:
# add_library(rules_engine STATIC src/rules_engine.cpp)
# target_link_libraries(rules_engine PUBLIC processing)
