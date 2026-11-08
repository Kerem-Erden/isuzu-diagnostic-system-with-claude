# gateway-core: gateway application logic and the deterministic simulated ECU.
# Compiled into the ESP32 firmware and into the desktop simulation source (ADR-003).
add_library(gateway_core STATIC
    src/gateway.cpp
    src/simulated_ecu.cpp)
add_library(isuzu::gateway_core ALIAS gateway_core)

target_include_directories(gateway_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(gateway_core PUBLIC isuzu::diagnostic_core isuzu::serial_protocol)
target_compile_features(gateway_core PUBLIC cxx_std_17)
isuzu_set_warnings(gateway_core)
