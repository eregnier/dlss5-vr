# Field Notes — RenoDX / ReShade Era (Archive)

> **[Archive]** This document summarizes the historical RenoDX + ReShade 6.8 phase of the project, which has been abandoned. Since **v1.1.0** the pipeline is OptiScaler Pre-SR, deployed by **`VR-DLSS5-Installer.exe`**; the former tools and the Go CLI were removed from the repository (the download cache moved to `%LOCALAPPDATA%\DLSS5-VR\cache`).
>
> This project does **not** decompile, patch or redistribute the LukeRoss mod or any NVIDIA binary. Everything below was observed from the mod's own logs, its public configuration files and the game's behaviour. Users obtain LukeRoss R.E.A.L. VR and the NVIDIA runtime from their official channels.

---

## 1. The Core Problem: the `dxgi.dll` Identity Conflict

- The **LukeRoss R.E.A.L. VR** mod installs itself as `dxgi.dll` and intercepts DirectX, cameras, OpenXR/OpenVR and stereoscopy.
- The old **DLSS 5 injector** (DLSS5oneclick / RenoDX) was built on ReShade 6.8+ with Add-on support and also wanted `dxgi.dll` to host `renodx-dlss5.addon64`.
- Installing one silently replaced the other: VR without DLSS 5, or DLSS 5 without VR.

## 2. The Proxy-Chaining Workaround

The retained solution was to chain proxies instead of competing for the same name:

```
Game.exe
 ├── dxgi.dll      -> RealVR64.dll        (LukeRoss keeps the swapchain/stereo priority)
 └── dinput8.dll   -> ReShade 6.8 Add-on  (hosts renodx-dlss5.addon64)
```

Key operational rules that came out of that phase (still valid today):

1. **Never overwrite `dxgi.dll` when a LukeRoss mod is present.** Detect the mod and keep it as the `dxgi.dll` proxy.
2. **Load the DLSS side before LukeRoss when any wrapping is involved**, so the NGX hooks are in place before the VR pipeline builds its handles.
3. **Never install inline detours into LukeRoss code.** Its own stereo/NGX handling must stay untouched; use export chaining or loading order instead. (Inline trampolines also broke Windows x64 stack walking in multi-threaded VR.)
4. **Avoid synchronous disk I/O on the render thread.** All INI writes and log flushes must stay off the hot path.
5. **Verify a headset pipeline end-to-end after every change**: a VR session that silently falls back to flat rendering looks like "performance fixed" while VR is actually disabled.

## 3. What the Logs Told Us (No Binary Analysis)

- `RealVR64.log` reports `OptiScaler.asi loaded and patched` when the OptiScaler engine is present: LukeRoss natively coordinates with it. This is the basis of the current Pre-SR architecture.
- `RealVR64.log` also reports feature initialization failures when an unknown NGX feature ID is requested; the modern OptiScaler engine avoids that path entirely.
- `ReShade.log` showed the add-on being repeatedly unloaded/reloaded when the swapchain was resized (menu → 3D world), which is one of the reasons the ReShade path was abandoned.
- Shortcut mapping observed from public INIs: LukeRoss overlay on `F1` (`KeyOverlay=112`), ReShade overlay on `Home`, the old DLSS 5 toggle on `F6`.

## 4. Why the ReShade Path Was Dropped

- ReShade adds a full present/swapchain interception layer on top of the VR pipeline, with unmanaged CPU/GPU overhead and resize-time reloads.
- The 165 MB neural runtime was evaluated **after** upscaling (Post-SR 4K per eye, ~29.5 Mpx stereo), which cannot fit the 13.88 ms budget at 72 Hz.
- OptiScaler Pre-SR evaluates the same model **before** upscaling (4.15 Mpx at `WorkingScale=0.75`) and cooperates natively with LukeRoss, without ReShade.

## 5. Current Recipe (v1.1.0)

1. Run `installer/VR-DLSS5-Installer.exe`, select the game executable, select your own `nvngx_dlssnr.dll` (NVIDIA runtime, not redistributed), then **Install / Update DLSS 5**.
2. The installer keeps LukeRoss as `RealVR64.dll`, deploys the OptiScaler Pre-SR engine, configures `OptiScaler.ini` and quarantines any legacy competing engine (`WINMM.dll`).
3. Open the HUD in game with `F6` or `Select+L3`; settings apply live through the shared-memory control channel.
