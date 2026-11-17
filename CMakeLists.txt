cmake_minimum_required(VERSION 3.16)
project(R2MEGA VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(R2MEGA_BUILD_GUI "Build the SDL3 / Dear ImGui application (not yet implemented)" OFF)

add_library(R2MEGA_core STATIC
    src/Core/Board.cpp
    src/Core/Cartridge/Cartridge.cpp
    src/Core/CPU/Moto68000/Moto68000.cpp
    src/Core/CPU/Z80/Z80.cpp
    src/Core/VDP/VDP.cpp
    src/Core/Audio/YM2612/YM2612.cpp
    src/Core/Audio/SN76489/SN76489.cpp
    src/Core/IO/ControllerPorts.cpp
)
target_compile_features(R2MEGA_core PUBLIC cxx_std_17)
target_include_directories(R2MEGA_core PUBLIC src)
if(MSVC)
    target_compile_options(R2MEGA_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(R2MEGA_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Compile the application placeholders independently of SDL and the core.
add_library(R2MEGA_application STATIC
    src/Systems/Engine.cpp
    src/Window/Window.cpp
)
target_compile_features(R2MEGA_application PUBLIC cxx_std_17)
target_include_directories(R2MEGA_application PUBLIC src)
if(MSVC)
    target_compile_options(R2MEGA_application PRIVATE /W4 /utf-8)
else()
    target_compile_options(R2MEGA_application PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(R2MEGA_BUILD_GUI)
    message(FATAL_ERROR "The SDL3 / Dear ImGui frontend has not been implemented yet.")
endif()

include(CTest)
