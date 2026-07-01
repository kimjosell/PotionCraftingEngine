cmake_minimum_required(VERSION 3.16)

project(effect_engine
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json 3.2.0 REQUIRED)

add_library(effect_engine
    src/engine_config.cpp
    src/config_loader.cpp
    src/engine.cpp
)

target_include_directories(effect_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(effect_engine
    PRIVATE
        nlohmann_json::nlohmann_json
)

add_executable(tea_alchemist src/main.cpp)
target_link_libraries(tea_alchemist PRIVATE effect_engine)

if(MSVC)
    target_compile_options(effect_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(effect_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
