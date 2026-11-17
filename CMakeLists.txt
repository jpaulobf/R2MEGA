cmake_minimum_required(VERSION 3.16)
project(R2MEGA VERSION 0.0.1 LANGUAGES C CXX)
set_source_files_properties(src/Window/Window.cpp PROPERTIES COMPILE_DEFINITIONS "R2_APP_VERSION=\"${PROJECT_VERSION}\"")

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(R2MEGA_BUILD_GUI "Build the SDL3 / Dear ImGui application" ON)

include(FetchContent)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    GIT_REPOSITORY https://github.com/madler/zlib.git
    GIT_TAG v1.3.1)
FetchContent_MakeAvailable(zlib)

add_library(R2MEGA_core STATIC
    src/*
)
target_compile_features(R2MEGA_core PUBLIC cxx_std_17)
target_include_directories(R2MEGA_core PUBLIC src PRIVATE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
target_link_libraries(R2MEGA_core PRIVATE zlibstatic)
if(MSVC)
    target_compile_options(R2MEGA_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(R2MEGA_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_library(R2MEGA_board_factory STATIC src/Systems/BoardFactory.cpp)
target_link_libraries(R2MEGA_board_factory PUBLIC R2MEGA_core)

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
        GIT_TAG 829a65d769d935c4852f8159e964312c0957260a # release-3.4.18
    )
    FetchContent_MakeAvailable(SDL3)
    FetchContent_Declare(imgui
        GIT_REPOSITORY https://github.com/ocornut/imgui.git
        GIT_TAG f1cc2ae15e53a861a874c3034aae6798fde194ab # v1.92.9b
    )
    FetchContent_MakeAvailable(imgui)
    add_library(R2MEGA_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    )
    target_include_directories(R2MEGA_imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_link_libraries(R2MEGA_imgui PUBLIC SDL3::SDL3-static)
    add_library(R2MEGA_window STATIC src/Window/KeyboardConfig.cpp src/Window/KeyboardDialog.cpp src/Systems/AtomicFile.cpp src/Window/VideoPresenter.cpp src/Window/DisplayMenu.cpp src/Window/Window.cpp src/Window/DebugWindow.cpp src/Window/FileManager.cpp src/Window/MemoryInspector.cpp src/Window/ShaderPreset.cpp src/Window/NtscShaderPipeline.cpp)
    target_compile_features(R2MEGA_window PUBLIC cxx_std_17)
    target_include_directories(R2MEGA_window PUBLIC src)
    target_link_libraries(R2MEGA_window PRIVATE R2MEGA_imgui R2MEGA_core)
    if(WIN32)
        target_link_libraries(R2MEGA_window PRIVATE comctl32 d3d11 d3dcompiler ole32 windowscodecs uuid)
    endif()
    if(MSVC)
        target_compile_options(R2MEGA_window PRIVATE /W4 /utf-8)
    else()
        target_compile_options(R2MEGA_window PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_library(R2MEGA_input STATIC src/Systems/Input.cpp)
    target_compile_features(R2MEGA_input PUBLIC cxx_std_17)
    target_include_directories(R2MEGA_input PUBLIC src)
    target_link_libraries(R2MEGA_input PUBLIC SDL3::SDL3-static)
    if(MSVC)
        target_compile_options(R2MEGA_input PRIVATE /W4 /utf-8)
    else()
        target_compile_options(R2MEGA_input PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_library(R2MEGA_audio STATIC src/Systems/Audio.cpp)
    target_compile_features(R2MEGA_audio PUBLIC cxx_std_17)
    target_include_directories(R2MEGA_audio PUBLIC src)
    target_link_libraries(R2MEGA_audio PUBLIC SDL3::SDL3-static)
    if(MSVC)
        target_compile_options(R2MEGA_audio PRIVATE /W4 /utf-8)
    else()
        target_compile_options(R2MEGA_audio PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_library(R2MEGA_save_state_store STATIC src/Systems/SaveStateStore.cpp)
    target_link_libraries(R2MEGA_save_state_store PUBLIC R2MEGA_core SDL3::SDL3-static)
    add_executable(R2MEGA src/Emulator.cpp src/Systems/Engine.cpp)
    add_custom_command(TARGET R2MEGA POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:R2MEGA>/shaders")
    if(WIN32)
        enable_language(RC)
        target_sources(R2MEGA PRIVATE resources/resource.rc)
        target_include_directories(R2MEGA PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/resources")
        set_property(SOURCE resources/resource.rc APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/logob.ico")
    endif()
    target_link_libraries(R2MEGA PRIVATE R2MEGA_window R2MEGA_board_factory R2MEGA_input R2MEGA_audio R2MEGA_save_state_store)
    set_target_properties(R2MEGA PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
endif()

include(CTest)
if(BUILD_TESTING)
    
endif()
