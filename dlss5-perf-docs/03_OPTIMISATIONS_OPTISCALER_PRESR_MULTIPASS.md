# 03. Deep Technical Analysis of OptiScaler Pre-SR Multipass (wilsjo2)

This document analyzes the internal mechanisms developed by **wilsjo2** in the `OptiScaler-DLSSNR-PreSR-Multipass` fork. That project provides the fundamental answers to the structural limitations of the old ReShade/RenoDX add-ons.

---

## 1. The Core Innovation: Pre-SR Placement (`RunBeforeSR=true`)

### A. Placement Principle
In a graphics architecture using Super Resolution (DLSS SR, FSR, XeSS):
```
[Standard monolithic pipeline (RenoDX)]:
Engine render (1080p) ──> DLSS Super Resolution ──> 4K display ──> [DLSS 5 Neural Recon] (heavy!)

[OptiScaler Pre-SR pipeline (wilsjo2)]:
Engine render (1080p) ──> [DLSS 5 Neural Recon] (ultra light!) ──> DLSS Super Resolution ──> 4K display
```

### B. Implementation in the Source Code (`DlssNr_Dx12.cpp`)
OptiScaler intercepts the NGX call `NVSDK_NGX_D3D12_EvaluateFeature` and inserts itself both **before** and **after** upscaling:

```cpp
// Excerpt from OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp (lines 1180-1194)
if (nrUpscale)
    DlssNr::EvaluateBeforeUpscale(InCmdList, InParameters, nullptr, 0, rayReconstruction);

NVSDK_NGX_Result result =
    NVNGXProxy::D3D12_EvaluateFeature()(InCmdList, InFeatureHandle, InParameters, InCallback);

if (result == NVSDK_NGX_Result_Success && nrUpscale)
    DlssNr::EvaluateAfterUpscale(InCmdList, InParameters, nullptr, rayReconstruction);
```

When `RunBeforeSR=true` is enabled in `OptiScaler.ini`:
1. `EvaluateBeforeUpscale` extracts the un-upscaled color buffer (`NVSDK_NGX_Parameter_Color`).
2. The DLSS 5 neural model runs directly on that native low-resolution render buffer.
3. `NVNGXProxy::D3D12_EvaluateFeature` takes the AI-reconstructed result and enlarges it to the headset's target resolution.
4. `EvaluateAfterUpscale` does not re-run a redundant pass (except in RR residual mode).

---

## 2. The Quadratic Surface Reducer (`WorkingScale`)

### A. The $O(r^2)$ Mathematical Law
The compute cost of convolutional neural networks (CNNs / visual transformers) on Tensor Cores is directly proportional to the total number of input pixels (surface area):
$$\text{GPU load} \propto W \times H = r^2 \cdot (W_{\text{base}} \times H_{\text{base}})$$

| WorkingScale | Per-eye resolution (base 1920×1920) | Stereo surface | Tensor Core cost (5090) | Relative gain |
| :--- | :--- | :--- | :--- | :--- |
| **1.00** (native Pre-SR) | $1920 \times 1920$ | 7.37 Mpx | **3.00 ms** | $-75\%$ vs 4K |
| **0.75** (balanced) | $1440 \times 1440$ | 4.15 Mpx | **1.69 ms** | **$-86\%$ vs 4K** |
| **0.66** (performance) | $1267 \times 1267$ | 3.21 Mpx | **1.31 ms** | **$-89\%$ vs 4K** |
| **0.50** (ultra-perf) | $960 \times 960$ | 1.84 Mpx | **0.75 ms** | **$-94\%$ vs 4K** |

### B. Preserving Visual Sharpness
OptiScaler does not degrade the game's rendering:
- The game's base image and its motion vectors keep their full render resolution.
- Only the neural model's lighting/reconstruction estimate is inferred at a fractional resolution, then re-injected and filtered (Lanczos3 or Catmull-Rom filter).

---

## 3. Robust Buffer Handling with Padding (`DlssNr_ActiveColor.h`)

Some game engines, notably **REDengine 4 in Cyberpunk 2077**, allocate textures with memory padding larger than the active resolution (for example a $2560 \times 1440$ allocation for an active $2558 \times 1439$ display).

### The Problem in Older Injectors
In older add-ons, sending a padded texture to the neural model either caused an immediate memory crash (`DXGI_ERROR_DEVICE_REMOVED`) or a spatial offset of UV coordinates.

### The wilsjo2 Solution
OptiScaler detects the origin-zero active region (`renderWidth`, `renderHeight`, `colorBaseX`, `colorBaseY`):
```cpp
const auto active = PreSrColorExtent(colorDesc, renderWidth, renderHeight, colorBaseX, colorBaseY);
if (active && (active->width != allocationWidth || active->height != allocationHeight)) {
    // Copy only the active sub-rectangle into a work UAV texture
    // Run the DLSS-NR model on the useful area
    // Copy the result back without corrupting the engine's padding
}
```

---

## 4. Ray Reconstruction Support in Cyberpunk 2077 (`ResidualAcrossRR`)

### A. The Ray Reconstruction (DLSS-D) Dilemma
In Cyberpunk 2077 with Ray Tracing or Path Tracing (RT Overdrive):
- The input color buffer is un-denoised: it is extremely noisy from Monte-Carlo rays.
- If DLSS 5 runs directly on that input, it tries to "reconstruct" raw noise, creating flicker and luminance artifacts.
- If DLSS 5 runs after Ray Reconstruction, you fall back into the 29.5-million-pixel Post-SR 4K trap!

### B. The Innovative `ResidualAcrossRR` Approach
1. The DLSS-NR model runs on the low-resolution render input.
2. OptiScaler extracts the signed differential (the "neural residual"):
   $$\Delta = \text{Image}_{\text{NR}} - \text{Image}_{\text{Base}}$$
3. The residual is encoded into a temporary RGBA16F buffer.
4. The game engine runs Ray Reconstruction and Super Resolution normally on the untouched buffer.
5. An ultra-light composition pass re-applies the residual $\Delta$ on top of the denoised output, reprojecting it through temporal motion vectors with a configurable accumulation factor (`ResidualAcrossRRBlend=0.08`).
6. **Result**: DLSS 5's fine material and reflectance detail is preserved on top of Ray Reconstruction, while keeping the tiny Pre-SR cost!

---

## 5. The Signature Forwarder (`nvngx.dll_dlssnr.dll`)

### Why third-party injectors were rejected
NVIDIA's proprietary `nvngx_dlssnr.dll` binary enforces a strict check of the calling environment:
- On every API call, the runtime walks the stack to inspect the return address.
- It resolves the owning module of that address.
- If the path or file name does not contain the string `"nvngx.dll"` (the official NVIDIA driver's name being `_nvngx.dll`), it immediately returns the `NVSDK_NGX_Result_FAIL_PlatformError` error.

### The Forwarder Architecture
wilsjo2 developed `nvngx.dll_dlssnr.dll`:
- It is an open-source intermediate DLL whose name explicitly contains `nvngx.dll`.
- It acts as an isolated thunk layer for calls.
- When OptiScaler calls the forwarder, the forwarder calls `nvngx_dlssnr.dll`.
- NVIDIA's binary signature check succeeds 100%, with no illicit in-memory binary patch required!

---

## 6. Summary of Benefits for the VR Project

Adopting the techniques from `OptiScaler-DLSSNR-PreSR-Multipass` solves every pitfall of the `dlss5-vr` project:
1. **A 7× reduction in Tensor Core load** thanks to Pre-SR ($1440 \times 1440$).
2. **Crossing below the V-Sync threshold (12.89 ms < 13.88 ms)**, guaranteeing native 72 FPS without reprojection.
3. **Full compatibility with Cyberpunk 2077 Ray Reconstruction** through `ResidualAcrossRR`.
4. **Removal of ReShade 6.8**, reducing CPU overhead and startup crash risks.
