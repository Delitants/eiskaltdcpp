Windows 11 + Visual Studio build (Qt GUI)
=========================================

This fork now includes a Visual Studio-ready CMake preset file at repo root:

  CMakePresets.json

It lets you open the repository directly in Visual Studio and configure the
Windows build without hand-writing long CMake command lines.

Requirements
------------

1) Visual Studio 2022 or 2026 (Desktop development with C++)
2) CMake 3.23+
3) Qt 6.x (tested with Qt 6.8.3 msvc2022_64)
4) vcpkg with required libraries

Fast setup script (PowerShell, Administrator):

  windows\setup-win11-buildenv.ps1

That script installs:
- VS 2022 + tools, when needed
- CMake + Ninja + Git + Python
- vcpkg + dependencies
- Qt 6.8.3
- helper build scripts under C:\src

Environment variables expected by presets
-----------------------------------------

Set these in your Windows environment before configuring:

- VCPKG_ROOT = C:\vcpkg
- Qt6_DIR    = C:\Qt\6.8.3\msvc2022_64

Visual Studio workflow
----------------------

1) Open Visual Studio
2) File -> Open -> Folder -> select repository root
3) In CMake configure preset picker select the preset matching your install:
     windows-vs2022-x64
     windows-vs2026-x64
4) Build with one of:
     build-windows-vs2022-release
     build-windows-vs2022-relwithdebinfo
     build-windows-vs2026-release
     build-windows-vs2026-relwithdebinfo

CLI workflow (same presets)
---------------------------

Run from the matching x64 Native Tools Command Prompt:

  cmake --preset windows-vs2022-x64
  cmake --build --preset build-windows-vs2022-release

Output binary location (default):

  out\build\windows-vs2022-x64\eiskaltdcpp-qt\Release\EiskaltDC++.exe

For Visual Studio 2026, replace the preset names with windows-vs2026-x64
and build-windows-vs2026-release. The default output path then uses
out\build\windows-vs2026-x64.

Notes
-----

- Current preset defaults are Qt6-only (USE_QT6=ON, USE_GTK3=OFF).
- Aspell is disabled in preset by default (USE_ASPELL=OFF) to simplify MSVC setup.
- If CMake fails to locate Qt, verify Qt6_DIR and restart VS so it reloads env vars.



cmake --preset windows-ninja-multi
cmake --build --preset build-windows-ninja-release
