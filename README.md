# DLSS 5 <> VR : Neural Reconstruction for LukeRoss R.E.A.L. VR

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Platform: Windows 64-bit](https://img.shields.io/badge/Platform-Windows%20x64-blue.svg)]()
[![Graphics: DirectX 12 / OpenVR](https://img.shields.io/badge/Graphics-DirectX%2012%20%7C%20OpenVR-brightgreen.svg)]()

Universal C++ dual-proxy and automated installer bridging **NVIDIA DLSS 5 Neural Reconstruction** (ReShade 6.8+ Add-on & RenoDX) with **LukeRoss R.E.A.L. VR mods** (OpenXR / SteamVR).

Includes a lightweight native Win32 GUI installer, safe swap mechanism, automatic model downloading, and an interactive in-game OpenVR 3D HUD.

---

## Features

- **Zero-Crash Dual-Proxy (`proxy/dxgi.dll`)** : Seamlessly forwards all 23 standard DXGI exports to LukeRoss (`RealVR64.dll`) and loads ReShade 6.8+ without hook collisions.
- **Runtime Memory Uncap** : Intercepts `NVSDK_NGX_D3D12_EvaluateFeature` at the DXGI boundary, ensuring continuous neural reconstruction on both stereo eyes at full VR refresh rates (72Hz, 80Hz, 90Hz, 120Hz).
- **In-Game Vector HUD ("DLSS 5 <> VR")** :
  - High-DPI Segoe UI GDI rendering with anti-aliasing.
  - **In VR** : Immersive 3D floating overlay (movable to Top, Bottom, World, or Head).
  - **On Desktop** : Ultra-light transparent layered window.
  - Controller toggle (`Select / Back + L3 / Stick Click`) and keyboard toggle (`Insert` / `F6`).
- **Universal Win32 GUI Installer (`installer/VR-DLSS5-Installer.exe`)** :
  - Ultra-lightweight (~190 KB native C++, zero dependencies, no .NET, no Electron, no Python).
  - Drag-and-drop game executable support.
  - Automatic detection of Unreal Engine subfolders (`Binaries/Win64`).
  - **Safe Swap (Idempotent)** : Preserves LukeRoss `dxgi.dll` as `RealVR64.dll` on first run; updates proxy safely on subsequent runs without overwriting LukeRoss.
  - **1-Click Rollback** : Instantly restore vanilla LukeRoss VR via the *Restore* button.
  - **Built-in HTTP Downloader** : Automatically fetches the ~160 MB DLSS 5 neural model (`nvngx_dlssnr.dll`) from official GitHub releases if missing.

---

## Quick Start (For Players)

1. Download the latest **`DLSS5-VR-Release.zip`** from the [Releases](../../releases) tab.
2. Extract the archive anywhere on your PC.
3. Run **`VR-DLSS5-Installer.exe`**.
4. Click **Browse...** (or drag and drop) and select your game's main executable (e.g., `Cyberpunk2077.exe`, `afop.exe`, etc.).
5. Click **Install / Update DLSS 5**.
6. Launch your game normally with your VR headset connected!

### In-Game Controls

| Action | VR Controller | Keyboard |
| :--- | :--- | :--- |
| **Toggle HUD On/Off** | `Select / Back` + `L3` (Stick Click) | `Insert` |
| **Toggle DLSS 5 On/Off** | `Select` (in HUD) | `F6` |
| **Menu Navigation** | `D-Pad Up / Down` | `Up / Down Arrows` |
| **Change HUD Position** | Row `HUD Pos` (`Top` / `Bottom` / `World` / `Head`) | `Left / Right Arrows` |
| **ReShade Overlay** | — | `Home` |
| **LukeRoss VR Settings** | Dedicated VR button | `Numpad` |

---

## Repository Structure

```
dlss5-vr/
├── installer/                     # Universal Win32 C++ Installer GUI (~190 KB)
│   ├── installer.cpp              # Win32 GUI, auto-downloader, safe swap & restore engine
│   ├── build.bat                  # 1-click MSVC compilation
│   └── README.md                  # Detailed installer documentation
│
├── proxy/                         # C++ Dual-Proxy (Core Runtime Component)
│   ├── proxy.cpp                  # Hooks DXGI, OpenVR HUD, GDI Segoe UI, Memory uncap
│   ├── proxy.def                  # 23 DXGI system exports
│   ├── openvr.h / openvr_api.dll  # OpenVR API headers & 64-bit runtime
│   ├── build.bat                  # 1-click MSVC compilation -> dxgi.dll
│   ├── deploy.ps1                 # Deployment script to game folder
│   └── README.md
│
├── deps/                          # Redistributable Helper Dependencies
│   ├── ReShade64_dlss5.dll         # ReShade 6.8+ with neutralized OpenVR hooks
│   ├── renodx-dlss5.addon64       # RenoDX DLSS 5 add-on
│   ├── cudart64_12.dll            # NVIDIA CUDA 12 runtime
│   └── README.md
│
├── vr-dlss5-patch/                # Headless Go CLI Automation Tool
│   ├── main.go, detector/, ...    # Go sources for CLI usage
│   └── README.md
│
├── tools/                         # Python Diagnostic Utilities
│   ├── diag.py                    # Live RAM hooks & process inspector
│   ├── tail_log.py                # Real-time ReShade & LukeRoss log monitor
│   ├── analyze_eval.py / patch_workset.py
│   └── README.md
│
├── package_release.bat            # Automated build & release packager (.zip)
├── LICENSE                        # MIT License + Third-Party Notices
└── README.md                      # This document
```

---

## Building from Source

### Prerequisites
- Windows 10/11 x64
- Microsoft Visual Studio 2022 (Community, BuildTools, Professional, or Enterprise with C++ workload)

### Building the Entire Release
Run the automated packaging script from the root directory:
```cmd
package_release.bat
```
This will:
1. Compile `proxy/dxgi.dll` with full optimizations (`/O2 /std:c++17`).
2. Compile `installer/VR-DLSS5-Installer.exe`.
3. Assemble the standalone package in `dist/DLSS5-VR-Release/` and zip it to `dist/DLSS5-VR-Release.zip`.

---

## Compatibility

- **Target Architecture**: 64-bit Windows PC games.
- **Graphics API**: DirectX 12 native (DirectX 11 supported with `dlss5-bridge.addon64`).
- **VR Runtimes**: SteamVR / OpenVR / OpenXR via LukeRoss R.E.A.L. VR.
- **Tested Hardware**: NVIDIA GeForce RTX 40 / 50 Series (RTX 5090 validated).
- **Supported Games**: Any DirectX 12 game supported by LukeRoss with native DLSS (*Avatar: Frontiers of Pandora*, *Cyberpunk 2077*, *Horizon Forbidden West*, *Spider-Man*, *Ghost of Tsushima*, *Hogwarts Legacy*, *Star Wars Outlaws*, etc.).

---

## Legal & Disclaimer

- This project is an independent enabler and proxy.
- It is **NOT** affiliated with, endorsed by, or partnered with LukeRoss, NVIDIA, Valve, or Microsoft.
- This project does **NOT** contain, redistribute, or reverse-engineer any proprietary LukeRoss binaries or code. Users must independently obtain their own legitimate copy of LukeRoss R.E.A.L. VR mods directly from LukeRoss's official distribution channels.
- All game titles, trademarks, and registered marks are the property of their respective owners.

## License

This project is licensed under the [MIT License](LICENSE).
