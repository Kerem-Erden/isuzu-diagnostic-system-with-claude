# Developer Setup

## Required tools

| Tool | Version | Used for |
|---|---|---|
| CMake | ≥ 3.25 (tested 4.3.1) | all host builds, presets |
| Ninja | any recent (Qt ships one in `C:\Qt\Tools\Ninja`) | generator |
| C++ compiler | C++17 for libs, C++20 for desktop; tested MinGW 13.1 (`C:\Qt\Tools\mingw1310_64`) | host |
| Qt | 6.x (tested 6.11.1 `mingw_64`) with **Core, Widgets, Sql, Test, SerialPort** | desktop (Phase 3+) |
| ESP-IDF | **6.0.2**, target `esp32` | firmware (Phase 2+) |

> Local machine status (2026-11-08): Qt 6.11.1 MinGW incl. Serial Port; ESP-IDF v6.1 (EIM).
> Qt Serial Port is added via `C:/Qt/MaintenanceTool.exe` → *Add or remove components* →
> Qt 6.11.1 → Additional Libraries (Qt Creator's "Serial Terminal" extension is NOT the library).

## Host build (libraries and unit tests, no Qt)

PowerShell:

```powershell
$env:PATH = "C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;$env:PATH"
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

## Desktop build (Phase 3+)

```powershell
$env:QT_ROOT = "C:\Qt\6.11.1\mingw_64"
$env:PATH = "$env:QT_ROOT\bin;C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;$env:PATH"
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

## Rules

* Build commands never flash or monitor a device. Flashing is a separate, explicit step
  (`docs/build-and-release.md`) that requires the user's permission.
* No absolute developer paths in CMake files; machine-specific values go into
  environment variables or an untracked `CMakeUserPresets.json`.
