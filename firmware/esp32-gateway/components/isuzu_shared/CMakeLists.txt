# Compiles the hardware-independent libraries from <repo>/libs into an ESP-IDF component.
# The source lists mirror libs/*/CMakeLists.txt; keep them in sync when adding files.
set(ISUZU_LIBS "${CMAKE_CURRENT_LIST_DIR}/../../../../libs")

idf_component_register(
    SRCS
        "${ISUZU_LIBS}/diagnostic-core/src/error.cpp"
        "${ISUZU_LIBS}/diagnostic-core/src/dtc.cpp"
        "${ISUZU_LIBS}/diagnostic-core/src/isotp.cpp"
        "${ISUZU_LIBS}/diagnostic-core/src/obd.cpp"
        "${ISUZU_LIBS}/diagnostic-core/src/obd_transaction.cpp"
        "${ISUZU_LIBS}/serial-protocol/src/frame.cpp"
        "${ISUZU_LIBS}/serial-protocol/src/message.cpp"
        "${ISUZU_LIBS}/gateway-core/src/bench.cpp"
        "${ISUZU_LIBS}/gateway-core/src/gateway.cpp"
        "${ISUZU_LIBS}/gateway-core/src/simulated_ecu.cpp"
    INCLUDE_DIRS
        "${ISUZU_LIBS}/diagnostic-core/include"
        "${ISUZU_LIBS}/serial-protocol/include"
        "${ISUZU_LIBS}/gateway-core/include")

# DIAG_ENABLE_MODE04 is deliberately NOT defined: Mode 04 code is not compiled (ADR-008).
