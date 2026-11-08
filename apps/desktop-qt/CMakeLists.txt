# Qt 6 desktop application (Qt Widgets, ADR-002). C++20.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Test)
qt_standard_project_setup()

# desktop_core: everything except the widgets, so services/clients are testable headless.
add_library(desktop_core STATIC
    src/transport/transport.hpp
    src/transport/simulation_transport.hpp
    src/transport/simulation_transport.cpp
    src/gateway/gateway_types.hpp
    src/gateway/gateway_types.cpp
    src/gateway/gateway_client.hpp
    src/gateway/gateway_client.cpp)
target_include_directories(desktop_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(desktop_core PUBLIC Qt6::Core isuzu::gateway_core)
target_compile_features(desktop_core PUBLIC cxx_std_20)
isuzu_set_warnings(desktop_core)

qt_add_executable(isuzu_diagnostic WIN32
    src/main.cpp
    src/ui/main_window.hpp
    src/ui/main_window.cpp)
target_link_libraries(isuzu_diagnostic PRIVATE desktop_core Qt6::Widgets)
isuzu_set_warnings(isuzu_diagnostic)

if(ISUZU_BUILD_TESTS)
    qt_add_executable(desktop_tests
        ${PROJECT_SOURCE_DIR}/tests/unit/desktop/test_gateway_client.cpp)
    target_link_libraries(desktop_tests PRIVATE desktop_core Qt6::Test)
    isuzu_set_warnings(desktop_tests)
    add_test(NAME desktop_tests COMMAND desktop_tests)
    # Windows has no rpath: make the Qt DLLs visible to CTest without copying them.
    set_tests_properties(desktop_tests PROPERTIES
        LABELS "unit;desktop;simulation"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
