# serial-protocol: protocol v1 framing and message codec, shared by firmware and desktop.
add_library(serial_protocol STATIC
    src/frame.cpp
    src/message.cpp)
add_library(isuzu::serial_protocol ALIAS serial_protocol)

target_include_directories(serial_protocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(serial_protocol PUBLIC isuzu::diagnostic_core)
target_compile_features(serial_protocol PUBLIC cxx_std_17)
isuzu_set_warnings(serial_protocol)
