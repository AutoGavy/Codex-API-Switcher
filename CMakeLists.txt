cmake_minimum_required(VERSION 3.20)
project(CodexApiSwitcher VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(RAYLIB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../raylib" CACHE PATH "Path to the raylib source tree")
if(NOT EXISTS "${RAYLIB_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "raylib was not found. Configure with -DRAYLIB_SOURCE_DIR=C:/path/to/raylib")
endif()

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory("${RAYLIB_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/raylib" EXCLUDE_FROM_ALL)

add_executable(codex_api_switcher WIN32
    src/main.cpp
    src/config_manager.cpp
    src/config_manager.h
    app.rc
    resource.h
)

target_link_libraries(codex_api_switcher PRIVATE raylib)
target_include_directories(codex_api_switcher PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}")

if(MSVC)
    target_compile_definitions(codex_api_switcher PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(codex_api_switcher PRIVATE /W4 /permissive-)
    if(WIN32)
        target_link_options(codex_api_switcher PRIVATE "/ENTRY:mainCRTStartup")
    endif()
else()
    target_compile_options(codex_api_switcher PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_link_libraries(codex_api_switcher PRIVATE shell32)
endif()

set_target_properties(codex_api_switcher PROPERTIES
    OUTPUT_NAME "Codex API Switcher"
)
