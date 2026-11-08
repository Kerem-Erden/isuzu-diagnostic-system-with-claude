# Known Limitations

_As of 2026-11-08._

## Validation

* **Nothing is vehicle-validated.** Mode 01 and Mode 03 are implemented and tested on the host, in
  simulation and over the real ESP32 serial link with the simulation firmware only.
* The TWAI/CAN path of the firmware (vehicle profile) has never run on hardware. The reference
  system's transceiver path failed its bench test (CRX stuck recessive) and has not been re-measured.
* The firmware builds with ESP-IDF **v6.1** locally; the target **6.0.2** build exists only as a CI job
  that has not run yet (nothing pushed).

## Scope

* One physically addressed ECU, 11-bit IDs, 500/250 kbit/s fixed at build time. No functional
  addressing (0x7DF), no 29-bit OBD, no J1939, no automatic ECU or bitrate discovery.
* ECU request/response IDs and bitrate cannot be changed from the desktop; the vehicle profile only
  selects the knowledge-base application (stated in the UI).
* Mode 01 PIDs: 0x04, 0x05, 0x0C, 0x0D, 0x42 only. No freeze frame (Mode 02), no pending/permanent DTCs
  (Mode 07/0A), no Mode 09 VIN.
* **Mode 04 is not available** in any build (by design, ADR-008).
* Live-data reference warnings (hysteresis/thresholds) from the reference MVP are not implemented.

## Firmware

* No bench self-test mode, no passive bus monitor (only responder frames can be traced), no
  automatic bus-off recovery (recovery happens when the node is recreated at the next listen-only /
  normal switch). See `vehicle-validation-plan.md` §1.
* A lost TX-completion event is counted in the same `rxdrop` counter as lost RX frames.
* UART0 is shared with the ESP-IDF console; logs are tolerated by the protocol but cost bandwidth.

## Desktop

* Developer log cannot be exported yet (W17); it is bounded to 2000 entries.
* Audit history stores DTC scans and sessions, not live-data samples or every protocol error.
* Deleting a `SerialTransport` whose worker is stuck in the driver blocks the UI for up to 2 s.
* UI text is English; learning documents are Turkish.
* Screens were reviewed from simulation screenshots (offscreen rendering); real-display rendering and
  user interaction are not yet tested systematically.
* The package bundles Qt 6.11.1 (MinGW); CI builds with Qt 6.8.3.

## Knowledge base

* Content was extracted from Isuzu service documents by the reference project; possible causes are
  guidance, not a diagnosis. Accuracy for a specific vehicle is not verified (RK-10).
