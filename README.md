# CuteXMPP

CuteXMPP is a cross-platform Qt Widgets desktop XMPP client for Windows, Linux, and macOS.

It currently includes:
- real XMPP login and in-band registration through QXmpp
- SASL2 / FAST credential caching when the server supports it
- roster loading from the server
- vCard avatar loading
- direct message sending to the server
- MAM archive loading for chat history previews and message timelines
- local chat folders/workspaces stored in client settings
- separate UI modules for auth, main window, and settings
- CMake presets and Python helper scripts for local builds

## Requirements

- CMake 3.24 or newer
- Qt 6.5 or newer with `Core`, `Gui`, and `Widgets`
- QXmpp 1.14 or newer
- MSYS2 `ucrt64` toolchain on Windows or Ninja on Linux/macOS
- Python 3.10 or newer for helper scripts

## Windows toolchain

Windows presets are configured for Qt6 and QXmpp from MSYS2 UCRT64. Required packages:

```powershell
C:\msys64\usr\bin\bash.exe -lc "pacman -Sy --needed --noconfirm mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qxmpp"
```

## Quick start

```powershell
py -3 scripts/configure.py
py -3 scripts/build.py
py -3 scripts/run.py
```

## Direct CMake usage

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
```

Use the matching `linux-*` or `macos-*` preset on other platforms.

## GitHub CI readiness

The project is already organized around stable CMake preset names and deterministic build directories, so the same preset commands can be reused later in GitHub Actions.

Recommended Windows CI steps:

```powershell
cmake --fresh --preset windows-debug
cmake --build --preset windows-debug --parallel
```
