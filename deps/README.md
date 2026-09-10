# Dependencies (`deps/`)

Pre-compiled runtime binaries bundled with DLSS 5 Neural Rendering VR.

- **`OptiScaler.dll`** - modified DLSS-NR Pre-SR OptiScaler build with the
  live VR control channel. **GPL-3.0-or-later**; source and patch in
  [`optiscaler-src/`](optiscaler-src/README.md).
- **`OptiScaler/`** - OptiScaler backends and kernels (XeSS, FidelityFX,
  DirectX 12 Agility SDK, NVFP4 hybrid kernels).
- **`OptiScaler.ini`** - default engine configuration.
- **`nvngx.dll_dlssnr.dll`** - open-source NGX forwarder for the DLSS 5
  Neural Rendering runtime.
- **`cudart64_12.dll`** - NVIDIA CUDA 12 runtime, required by
  `nvngx_dlssnr.dll`; redistributed under the CUDA EULA.
- **`LICENSES/`** - full third-party license texts.

The NVIDIA DLSS 5 runtime (`nvngx_dlssnr.dll`) is provided by the user and is
never redistributed here.
