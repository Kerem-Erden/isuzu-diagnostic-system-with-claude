# diagnostic-core: hardware-, OS- and Qt-independent CAN / ISO-TP / OBD / DTC logic.
# The same source list is compiled by the ESP-IDF component in firmware/esp32-gateway.
add_library(diagnostic_core STATIC
    src/error.cpp
    src/dtc.cpp
    src/isotp.cpp
    src/obd.cpp
    src/obd_transaction.cpp)
add_library(isuzu::diagnostic_core ALIAS diagnostic_core)

target_include_directories(diagnostic_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(diagnostic_core PUBLIC cxx_std_17)
isuzu_set_warnings(diagnostic_core)
