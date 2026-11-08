# Build and Release

Build, flash and monitor are **separate** steps. Nothing in this repository flashes a device
automatically. Flashing or connecting to a vehicle requires the user's explicit permission in
the current session (brief §4).

## 1. Host libraries and tests

See `developer-setup.md`. `cmake --preset host-debug && cmake --build --preset host-debug && ctest --preset host-debug`.

## 2. Firmware (ESP-IDF 6.0.2, target esp32)

Profiles are layered `sdkconfig.defaults` files. The generated `sdkconfig` lives inside the
build directory, so profiles never overwrite each other and nothing generated is committed.

| Profile | Command (from `firmware/esp32-gateway`) | CAN activity |
|---|---|---|
| Simulation | `idf.py -B build-sim -D SDKCONFIG=build-sim/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.simulation" build` | none (TWAI not initialised) |
| Vehicle | `idf.py -B build-vehicle -D SDKCONFIG=build-vehicle/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.vehicle" build` | listen-only at boot; TX only for desktop requests |

Neither profile contains OBD Mode 04.

### Linux VM workflow (current developer setup)

ESP-IDF runs in a Linux VM; the Qt desktop runs on Windows.

1. Bring the repository into the VM (shared folder, `git bundle`, or clone after the user pushes).
   Keep the build directory on the VM's own disk if the shared folder has permission/symlink issues
   (`-B ~/isuzu-build/sim`).
2. `. $HOME/esp/esp-idf/export.sh` (path depends on the installation) and check
   `idf.py --version` → `v6.0.2`.
3. Build a profile (table above).
4. **Flash – only with explicit permission:** attach the CP210x USB device to the VM, then
   `idf.py -B build-sim -p /dev/ttyUSB0 flash`. Use `monitor` only for a short check; it holds the port.
5. Detach the USB device from the VM and give it back to Windows. The COM port number may change.
   The desktop must not try to open the port while the VM owns it (reference repo lesson `70ba8cc`).

### Expected serial output after boot (simulation)

The ROM/bootloader prints noise first; the desktop ignores it. The first protocol frame is:

```
#EVT|0|BOOT|proto=1|source=SIMULATION*XXXX
```

## 3. Desktop (Qt 6, Windows)

To be completed in Phase 3 (windeployqt packaging).

## 4. CI

`.github/workflows/ci.yml`:
* `host-tests`: GCC and Clang on Ubuntu with ASan/UBSan, all CTest suites.
* `firmware`: ESP-IDF 6.0.2 build of the simulation and vehicle profiles (no flashing).

## 5. Local environment notes (2026-11-08)

* Windows ESP-IDF **v6.1** (EIM, `C:/esp/v6.1/esp-idf`) builds both profiles. Activate in PowerShell:
  `. C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1`, then use the commands in §2.
  The project targets 6.0.2 (CI); 6.1 is used locally as the closest available version.
* The older v5.5.4 install has a broken Python environment (`No module named 'rich'`) and is unused.
