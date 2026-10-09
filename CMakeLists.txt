cmake_minimum_required(VERSION 3.16)

project(effect_engine
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On macOS, build universal (arm64 + x86_64) so the .dylib links into Godot/Unity/Unreal
# universal builds on both Apple Silicon and Intel. Override with -DCMAKE_OSX_ARCHITECTURES=arm64.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

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

# Shared library exposing the C API for FFI (Godot GDExtension, Unity P/Invoke, Unreal).
# Produces .dll / .so / .dylib. Only the extern "C" symbols are exported (visibility hidden).
add_library(effect_engine_c SHARED
    src/engine_config.cpp
    src/config_loader.cpp
    src/engine.cpp
    src/effect_engine_c.cpp
)
target_include_directories(effect_engine_c
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(effect_engine_c PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(effect_engine_c PRIVATE EFFECT_ENGINE_BUILD)
set_target_properties(effect_engine_c PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Smoke test driving the engine through the C API only.
add_executable(c_api_smoke src/c_api_smoke.cpp)
target_link_libraries(c_api_smoke PRIVATE effect_engine_c)

if(MSVC)
    target_compile_options(effect_engine PRIVATE /W4 /permissive-)
    target_compile_options(effect_engine_c PRIVATE /W4 /permissive-)
else()
    target_compile_options(effect_engine PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(effect_engine_c PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Micro-benchmarks (time, allocations, thread scaling). Build with -DCMAKE_BUILD_TYPE=Release.
add_executable(effect_engine_bench src/benchmark.cpp)
target_link_libraries(effect_engine_bench PRIVATE effect_engine)
