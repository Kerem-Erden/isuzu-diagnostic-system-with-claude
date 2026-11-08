# Testing Guide

## Levels

| Level | Location | Framework | Hardware |
|---|---|---|---|
| Core unit | `tests/unit/core` | in-repo harness (`tests/unit/test_harness.hpp`, ADR-006) | none |
| Protocol unit | `tests/unit/serial_protocol` | in-repo harness | none |
| Gateway unit | `tests/unit/gateway` | in-repo harness | none |
| Desktop unit | `tests/unit/desktop` | Qt Test | none |
| Integration | `tests/integration` | Qt Test | none (simulated ECU) |
| ESP32 bench | `docs/vehicle-validation-plan.md` | manual | ESP32, **user permission** |
| Vehicle | `docs/vehicle-validation-plan.md` | manual | vehicle, **user permission** |

Passing automated tests never implies vehicle support.

## Running

```bash
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug                     # all host tests
ctest --preset host-debug -L core             # by label
build/host-debug/tests/core_tests.exe isotp   # substring filter inside one executable
```

On this Windows machine the host toolchain is Qt's MinGW: put
`C:\Qt\Tools\mingw1310_64\bin` and `C:\Qt\Tools\Ninja` on `PATH` first.

## Core coverage (Phase 1)

| Brief §16 case | Test |
|---|---|
| TX enqueue failure | `transaction_enqueue_failure_is_can_enqueue_stage` |
| TX completion success / failure / missing | `transaction_happy_path_rpm`, `transaction_tx_completion_failure_is_reported`, `transaction_missing_tx_completion_is_not_blamed_on_ecu` |
| Timeout | `isotp_first_frame_timeout_is_rx_timeout`, `isotp_consecutive_gap_timeout_reports_received_bytes`, `isotp_late_frame_counts_as_timeout`, `transaction_silent_ecu_is_rx_timeout` |
| ISO-TP single / multi-frame, flow control, block size | `isotp_receives_single_frame`, `isotp_multi_frame_sends_flow_control_and_reassembles`, `isotp_block_size_requests_new_flow_control` |
| Bad sequence, wrap | `isotp_bad_sequence_fails_with_expected_and_received`, `isotp_sequence_number_wraps_from_15_to_0` |
| NRC 0x78 and other NRC | `transaction_response_pending_extends_deadline_to_p2_star`, `transaction_response_pending_is_bounded`, `transaction_negative_response_keeps_nrc` |
| Zero / one / many DTC | `mode03_zero_dtcs`, `mode03_one_and_many_dtcs` |
| Unrelated / wrong / stale frames | `isotp_ignores_unrelated_wrong_responder_and_stale_frames` |
| RX overflow | `isotp_overflow_answers_flow_control_overflow` (driver RX-queue overflow: firmware phase) |
| Mode 01 PID decode | `mode01_decodes_all_mvp_pids`, `mode01_supported_pid_bitmap` |
| Malformed payload | `mode01_rejects_wrong_service_pid_and_length`, `mode03_rejects_malformed_payloads`, `isotp_rejects_invalid_lengths` |
| Buffer lifetime | not applicable to the pure core (no driver-borrowed buffers); covered in the firmware TWAI adapter phase |

## Protocol and gateway coverage (Phase 1b / 2a)

| Area | Tests |
|---|---|
| Serial framing, partial reads, multiple frames per read, malformed frame, resync, oversize | `tests/unit/serial_protocol/test_frame.cpp` (13 tests) |
| Message parsing, structured errors, limits | `tests/unit/serial_protocol/test_message.cpp` (8 tests) |
| Gateway end-to-end against `SimulatedEcu` (all scenarios) | `tests/unit/gateway/test_gateway.cpp` (25 tests) |

`gateway_tests` carries the `simulation` label: these results are simulation evidence only.

## Desktop (Qt Test)

`tests/unit/desktop/test_gateway_client.cpp` – 11 cases, GatewayClient over SimulationTransport
(real gateway-core + SimulatedEcu). Labels `unit;desktop;simulation`. Requires the `windows-debug` preset.

## Hardware bench (opt-in)

`tests/hardware/test_serial_bench.cpp` (`bench_serial_tests`, labels `hardware;bench`) runs only when
`ISUZU_BENCH_PORT` is set; otherwise it skips, so plain `ctest` never touches a device. It aborts if the
gateway reports `source=VEHICLE`. Requires the **simulation** firmware on the ESP32:

```powershell
$env:ISUZU_BENCH_PORT = "COM5"
ctest --preset windows-debug -L bench --output-on-failure
```

## Known gaps

* Sanitizers (ASan/UBSan) are not available with MinGW; CI runs the host tests on Linux GCC
  with `-fsanitize=address,undefined` (Phase 5).
