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

| Bench (W15) | `idf.py -B build-bench -D SDKCONFIG=build-bench/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.bench" build` | **drives CTX + one self-test frame – vehicle must be disconnected** |

No profile contains OBD Mode 04. The bench profile reports `source=BENCH` and is refused by the desktop.

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

### Flashing from Windows (done once, 2026-11-08, with permission)

```powershell
. C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1
cd firmware/esp32-gateway
idf.py -B build-sim -D SDKCONFIG=build-sim/sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.simulation" -p COM5 flash
```

Check first that the port is the CP210x (`Get-CimInstance Win32_PnPEntity | ? Name -match 'COM'`) and that
the board is not connected to a vehicle. Close the desktop app first – only one program can own the port.

## 3. Desktop (Qt 6, Windows)

Development build: `developer-setup.md`. Release package:

```powershell
tools/package-windows.ps1 -QtRoot C:/Qt/6.11.1/mingw_64 -MinGwBin C:/Qt/Tools/mingw1310_64/bin -NinjaDir C:/Qt/Tools/Ninja
```

The script configures `windows-release`, builds, **runs all tests** (no package from a failing
build), copies the exe and `data/isuzu-knowledge.db`, runs `windeployqt --release --compiler-runtime`
(only the QSQLITE driver is kept), writes `PACKAGE.txt` (version, commit, Qt version, knowledge DB
SHA-256, validation warning) and zips `dist/IsuzuDiagnostic-<version>-<commit>-win64`.

Verified 2026-11-08: the unpacked folder runs with only `C:\Windows\System32` on `PATH` (no Qt, no
MinGW): connects to the simulation, finds the knowledge DB, saves audit rows. `dist/` is not committed.

## 4. CI

`.github/workflows/ci.yml`:
* `host-tests`: GCC and Clang on Ubuntu with ASan/UBSan, all CTest suites.
* `firmware`: ESP-IDF 6.0.2 build of the simulation, vehicle and bench profiles (no flashing).

## 5. Local environment notes (2026-11-08)

* Windows ESP-IDF **v6.1** (EIM, `C:/esp/v6.1/esp-idf`) builds both profiles. Activate in PowerShell:
  `. C:/Espressif/tools/Microsoft.v6.1.PowerShell_profile.ps1`, then use the commands in §2.
  The project targets 6.0.2 (CI); 6.1 is used locally as the closest available version.
* The older v5.5.4 install has a broken Python environment (`No module named 'rich'`) and is unused.
