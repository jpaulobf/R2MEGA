cmake_minimum_required(VERSION 3.16)
project(R2MEGA VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(R2MEGA_BUILD_GUI "Build the SDL3 / Dear ImGui application" ON)

include(FetchContent)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib GIT_REPOSITORY https://github.com/madler/zlib.git GIT_TAG v1.3.1)
FetchContent_MakeAvailable(zlib)

add_library(R2MEGA_core STATIC
    src/Common/RomFile.cpp
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
target_include_directories(R2MEGA_core PUBLIC src PRIVATE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
target_link_libraries(R2MEGA_core PRIVATE zlibstatic)
if(MSVC)
    target_compile_options(R2MEGA_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(R2MEGA_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(R2MEGA_BUILD_GUI)
    include(FetchContent)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    if(WIN32)
        set(SDL_CCACHE OFF CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG 829a65d769d935c4852f8159e964312c0957260a)
    FetchContent_MakeAvailable(SDL3)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG f1cc2ae15e53a861a874c3034aae6798fde194ab)
    FetchContent_MakeAvailable(imgui)
    add_library(R2MEGA_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp)
    target_include_directories(R2MEGA_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(R2MEGA_imgui PUBLIC SDL3::SDL3-static)
    add_library(R2MEGA_application STATIC src/Systems/Engine.cpp src/Window/Window.cpp src/Window/FileManager.cpp src/Window/RomPreview.cpp)
    target_compile_features(R2MEGA_application PUBLIC cxx_std_17)
    target_include_directories(R2MEGA_application PUBLIC src)
    target_link_libraries(R2MEGA_application PRIVATE R2MEGA_imgui R2MEGA_core)
    target_compile_definitions(R2MEGA_application PRIVATE R2_APP_VERSION="${PROJECT_VERSION}")
    if(MSVC)
        target_compile_options(R2MEGA_application PRIVATE /W4 /utf-8)
    else()
        target_compile_options(R2MEGA_application PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_executable(R2MEGA src/Emulator.cpp)
    target_link_libraries(R2MEGA PRIVATE R2MEGA_application)
endif()
include(CTest)
if(BUILD_TESTING)
    add_executable(rom_tests tests/RomTests.cpp)
    target_link_libraries(rom_tests PRIVATE R2MEGA_core zlibstatic)
    add_test(NAME rom_tests COMMAND rom_tests ${CMAKE_CURRENT_BINARY_DIR}/rom-test-fixtures)
endif()

if(BUILD_TESTING AND R2MEGA_BUILD_GUI)
    add_executable(window_smoke_tests tests/WindowSmokeTests.cpp)
    target_link_libraries(window_smoke_tests PRIVATE R2MEGA_application R2MEGA_imgui)
    add_test(NAME window_smoke_tests COMMAND window_smoke_tests)
    set_tests_properties(window_smoke_tests PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_RENDER_DRIVER=software")
endif()
