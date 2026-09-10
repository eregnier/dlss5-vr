# Third-Party License Texts

Full license texts for the third-party components redistributed in the
release package (`deps/`, `proxy/`).

| Component | File |
| --- | --- |
| OptiScaler + DLSS-NR Pre-SR fork (GPL-3.0-or-later) | `GPL-3.0-or-later.txt` |
| Intel XeSS runtime (`libxess*.dll`, `libxell.dll`) | `XeSS_LICENSE.txt` |
| AMD FidelityFX (`amd_fidelityfx_*.dll`) | `AMD-FidelityFX-MIT.txt`, `FidelityFX_v2_LICENSE.md` |
| Microsoft DirectX 12 Agility SDK (`D3D12Core.dll`) | `DirectX_LICENSE.txt` |
| Khronos headers (via AMD FidelityFX) | `Khronos-Apache-2.0.txt`, `Khronos-MIT.txt` |
| NVIDIA Optical Flow headers (via AMD FidelityFX) | `NVIDIA-Optical-Flow-Headers.txt` |
| RenoDX attribution (colour processing in OptiScaler) | `RenoDX_ATTRIBUTION.txt` |
| NVIDIA CUDA runtime (`cudart64_12.dll`) | `NVIDIA-CUDA-Redistribution-Notice.txt` |
| MinHook (bundled in `proxy/`) | `MinHook-BSD-2-Clause.txt` |
| OpenVR SDK / `openvr_api.dll` | `OpenVR-BSD-3-Clause.txt` |

Additional notices for components linked inside `OptiScaler.dll`, as
documented by the upstream project:

- FreeType, licensed under the FTL:
  https://gitlab.freedesktop.org/freetype/freetype/-/blob/master/docs/FTL.TXT
- OptiScaler is distributed under the GNU GPL-3.0-or-later. The corresponding
  source for the modified build shipped here is the upstream repository at
  commit `1b1dd650` (https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass)
  plus the patch in `../optiscaler-src/`.

The NVIDIA DLSS 5 Neural Rendering runtime (`nvngx_dlssnr.dll`) is **not**
included or redistributed by this project.
