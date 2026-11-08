#pragma once
/**
 * @file bench_app.hpp
 * @brief BENCH self-test firmware entry (W15, profile sdkconfig.defaults.bench).
 *
 * WARNING: drives CTX and transmits one self-test frame – CANH/CANL must not be connected to a
 * vehicle. Speaks protocol v1 with three commands: HELLO (source=BENCH), PING, BENCH_RUN.
 * Interpretation of the measurements is gw::bench::evaluate() (host-tested).
 */

#include "uart_transport.hpp"

/// Never returns.
[[noreturn]] void runBenchApp(UartTransport& uart);
