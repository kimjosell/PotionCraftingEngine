cmake_minimum_required(VERSION 3.16)

project(effect_engine
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(effect_engine
    src/engine_config.cpp
    src/config_loader.cpp
)

target_include_directories(effect_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(effect_engine
    PRIVATE
        nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(effect_engine PRIVATE /W4 /permissive-)
else()
    target_compile_options(effect_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()
