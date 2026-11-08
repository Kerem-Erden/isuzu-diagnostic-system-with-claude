# Qt 6 desktop application (Qt Widgets, ADR-002). C++20.
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets SerialPort Sql Test)
qt_standard_project_setup()

set(ISUZU_KNOWLEDGE_DB "${PROJECT_SOURCE_DIR}/data/knowledge/isuzu-knowledge.db")

# desktop_core: everything except the widgets, so services/clients are testable headless.
add_library(desktop_core STATIC
    src/transport/transport.hpp
    src/transport/simulation_transport.hpp
    src/transport/simulation_transport.cpp
    src/transport/serial_transport.hpp
    src/transport/serial_transport.cpp
    src/gateway/gateway_types.hpp
    src/gateway/gateway_types.cpp
    src/gateway/gateway_client.hpp
    src/gateway/gateway_client.cpp
    src/data/audit_store.hpp
    src/data/audit_store.cpp
    src/services/dtc_scan_service.hpp
    src/services/dtc_scan_service.cpp
    src/services/live_data_service.hpp
    src/services/live_data_service.cpp)
target_include_directories(desktop_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(desktop_core PUBLIC Qt6::Core Qt6::SerialPort Qt6::Sql isuzu::gateway_core isuzu::dtc_knowledge)
target_compile_features(desktop_core PUBLIC cxx_std_20)
isuzu_set_warnings(desktop_core)

qt_add_executable(isuzu_diagnostic WIN32
    src/main.cpp
    src/ui/main_window.hpp
    src/ui/main_window.cpp)
target_link_libraries(isuzu_diagnostic PRIVATE desktop_core Qt6::Widgets)
isuzu_set_warnings(isuzu_diagnostic)
# The knowledge DB ships next to the executable (data/isuzu-knowledge.db), read-only at runtime.
add_custom_command(TARGET isuzu_diagnostic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:isuzu_diagnostic>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ISUZU_KNOWLEDGE_DB}" "$<TARGET_FILE_DIR:isuzu_diagnostic>/data/"
    VERBATIM)

if(ISUZU_BUILD_TESTS)
    # isuzu_add_qt_test(<name> <source> <labels>): one Qt Test executable per test class.
    function(isuzu_add_qt_test name source labels)
        qt_add_executable(${name} ${PROJECT_SOURCE_DIR}/tests/${source})
        target_link_libraries(${name} PRIVATE desktop_core Qt6::Test)
        target_compile_definitions(${name} PRIVATE ISUZU_KNOWLEDGE_DB="${ISUZU_KNOWLEDGE_DB}")
        isuzu_set_warnings(${name})
        add_test(NAME ${name} COMMAND ${name})
        # Windows has no rpath: make the Qt DLLs visible to CTest without copying them.
        set_tests_properties(${name} PROPERTIES
            LABELS "${labels}"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endfunction()

    isuzu_add_qt_test(desktop_tests unit/desktop/test_gateway_client.cpp "unit;desktop;simulation")
    isuzu_add_qt_test(desktop_serial_tests unit/desktop/test_serial_transport.cpp "unit;desktop")
    isuzu_add_qt_test(desktop_data_tests unit/desktop/test_data_layer.cpp "unit;desktop;database;simulation")
    isuzu_add_qt_test(desktop_live_tests unit/desktop/test_live_data.cpp "unit;desktop;simulation")
    # Skips itself unless ISUZU_BENCH_PORT names a COM port with the SIMULATION firmware attached.
    isuzu_add_qt_test(bench_serial_tests hardware/test_serial_bench.cpp "hardware;bench")
endif()
