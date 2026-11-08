cmake_minimum_required(VERSION 3.21)

project(IsuzuDiagnosticSystem
    VERSION 0.1.0
    DESCRIPTION "ESP32 CAN gateway and Qt desktop for Isuzu OBD diagnostics"
    LANGUAGES CXX)

# Shared libraries are C++17 so the ESP-IDF firmware can compile them unchanged (ADR-003).
# The desktop application raises its own standard to C++20.
set(CMAKE_CXX_EXTENSIONS OFF)

option(ISUZU_BUILD_TESTS "Build unit and integration tests" ON)
option(ISUZU_BUILD_DESKTOP "Build the Qt 6 desktop application (requires Qt)" OFF)
option(ISUZU_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in project targets" ON)

include(cmake/ProjectWarnings.cmake)

add_subdirectory(libs/diagnostic-core)
add_subdirectory(libs/serial-protocol)

if(ISUZU_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
