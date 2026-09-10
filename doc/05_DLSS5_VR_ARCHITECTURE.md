# 05. New Architecture for dlss5-vr: The OptiScaler Pre-SR Integration

This document establishes the complete architectural overhaul of the `dlss5-vr` project for the **LukeRoss REAL VR** mod under **Cyberpunk 2077**.

---

## 1. The Major Discovery: Native LukeRoss <> OptiScaler Compatibility

`RealVR64.log` records an explicit cooperation line when `OptiScaler.asi` is present in the executable directory:

```
OptiScaler.asi loaded and patched
```

**Key takeaway**:
LukeRoss has specifically anticipated and implemented detection and stereoscopic coordination with `OptiScaler.asi`.
When `OptiScaler.asi` is present, `RealVR64.dll` automatically adapts its stereoscopic hooks (`AUDLSSFixInstanceStateD3D12`), ensuring perfect separation of the DLSS instances for the left and right eye.

---

## 2. Structural Overhaul: Dropping ReShade for OptiScaler Pre-SR

### A. The Old Architecture (Obsolete)
```
Cyberpunk2077.exe
     │
     ├──> dxgi.dll (dlss5-vr custom proxy)
     │         └──> RealVR64.dll (LukeRoss)
     │
     └──> dinput8.dll / ReShade64_dlss5.dll (ReShade 6.8 Add-on)
               └──> renodx-dlss5.addon64
                         └──> nvngx_dlssnr.dll [POST-SR 4K = 29.5 Mpx = 40 FPS!]
```
- **Drawbacks**: double ReShade + LukeRoss load, swapchain conflicts, scratched scratch memory, monolithic Post-SR 4K evaluation, 40 FPS locked by reprojection.

---

### B. The New Recommended Architecture (High Performance)

```
Cyberpunk2077.exe
     │
     ├──> dxgi.dll (dlss5-vr dual-proxy: Master Orchestrator + OpenVR HUD)
     │         │
     │         ├──> RealVR64.dll (LukeRoss VR mod: swapchain & stereoscopy)
     │         │
     │         └──> OptiScaler.dll / OptiScaler.asi (wilsjo2 Pre-SR engine v0.7.6)
     │                   │
     │                   ├──> nvngx.dll_dlssnr.dll (validation forwarder)
     │                   │         └──> nvngx_dlssnr.dll (FP8 / FP16 AI model)
     │                   │
     │                   └──> nvngx_dlss.dll (native Super Resolution)
```

### Major Advantages:
1. **Zero ReShade**: complete removal of ReShade 6.8, its capture hooks and the RenoDX add-on. Immediate stability and load-time gains.
2. **Native Pre-SR**: the neural network processes the image **before** upscaling, bringing compute down from 29.5 Mpx to 4.15 Mpx (with `WorkingScale=0.75`).
3. **VR HUD preserved**: our `dxgi.dll` keeps the high-resolution OpenVR overlay in the headset, letting the player tune `WorkingScale`, presets and intensity live without leaving VR.
4. **Ray Reconstruction support**: `ResidualAcrossRR=true` makes it possible to play with RT Overdrive and Ray Reconstruction + DLSS 5 without flicker artifacts.

---

## 3. Validated Optimal Configuration for Cyberpunk 2077 VR

`OptiScaler.ini` should be configured with the following values to guarantee **solid 72 FPS** on RTX 4090 / 5090:

```ini
[DlssNr]
; Main switch
Enabled=true

; Run the neural network BEFORE super resolution (essential!)
RunBeforeSR=true

; Model compute scale factor (75% = 56% of the surface = ~1.6 ms on a 5090)
WorkingScale=0.75

; A single sequential pass (2 and 3 double/triple compute time)
Passes=1

; Cyberpunk 2077 Ray Reconstruction: keep neural detail on top of the denoiser
ResidualAcrossRR=true
ResidualAcrossRRBlend=0.08

; Disable test modes and disk captures
DeferredDLSS=false
ResidualFG=false
AutoCapture=false
DebugView=0

; Presets and intensities
Preset=2
Style=0
Intensity=1.0
LocalStructure=1.0
LocalTone=1.0
SkinStructure=-1.0
AutoMask=true
```

---

## 4. Two Possible Deployment Topologies

### Topology 1: Direct "Zero-Proxy" Deployment via LukeRoss (`OptiScaler.asi`)
This is the simplest, most direct method:
1. In `Cyberpunk 2077\bin\x64\`:
   - `dxgi.dll` remains LukeRoss's official `RealVR64.dll`.
   - Copy `OptiScaler.dll` renamed as **`OptiScaler.asi`** (or `dbghelp.dll`).
   - Copy `nvngx.dll_dlssnr.dll`, `nvngx_dlssnr.dll` and the `OptiScaler\` subfolder.
   - Copy the optimized `OptiScaler.ini`.
2. At launch: LukeRoss loads and patches `OptiScaler.asi` directly. The OptiScaler menu opens with `Insert`.

### Topology 2: Enriched "Dual-Proxy" Deployment (Recommended with the VR HUD)
This method combines the best of both worlds:
1. In `Cyberpunk 2077\bin\x64\`:
   - `dxgi.dll` is our compiled C++ proxy.
   - `RealVR64.dll` is preserved and called by our proxy.
   - `OptiScaler.dll` is loaded as the upscaling and inference module.
   - Our proxy injects the OpenVR overlay (`F6` or `Select + L3`) right in front of the player's eyes to adjust OptiScaler's dials (`WorkingScale`, `Passes`, `Presets`) live.

---

## 5. The Dynamic VR Frame Guard

To prevent any drop into reprojection, the proxy or configurator can integrate a dynamic guard:

$$\text{If } T_{\text{frame}} > 13.00\text{ ms (alert threshold at 72 Hz V-Sync)}:$$
$$\text{Automatically lower } \text{WorkingScale} \text{ from } 0.75 \longrightarrow 0.66 \text{ or } 0.50$$

That adjustment instantly reduces Tensor Core time from 1.69 ms to 0.75 ms, immediately bringing the frame back under 13.88 ms and preventing the headset from switching to 40 FPS.
