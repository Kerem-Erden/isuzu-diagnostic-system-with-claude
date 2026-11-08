# Host unit tests for the shared libraries. They never touch hardware: CAN frames are built in
# memory and time is passed explicitly, so results are deterministic on every machine and in CI.
add_executable(core_tests
    unit/test_main.cpp
    unit/core/test_error.cpp
    unit/core/test_dtc.cpp
    unit/core/test_isotp.cpp
    unit/core/test_obd.cpp
    unit/core/test_obd_transaction.cpp
    unit/core/test_mode04_simulation_only.cpp)
target_include_directories(core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(core_tests PRIVATE isuzu::diagnostic_core)
isuzu_set_warnings(core_tests)
add_test(NAME core_tests COMMAND core_tests)
set_tests_properties(core_tests PROPERTIES LABELS "unit;core")

add_executable(protocol_tests
    unit/test_main.cpp
    unit/serial_protocol/test_frame.cpp
    unit/serial_protocol/test_message.cpp)
target_include_directories(protocol_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/unit)
target_link_libraries(protocol_tests PRIVATE isuzu::serial_protocol)
isuzu_set_warnings(protocol_tests)
add_test(NAME protocol_tests COMMAND protocol_tests)
set_tests_properties(protocol_tests PROPERTIES LABELS "unit;protocol")
