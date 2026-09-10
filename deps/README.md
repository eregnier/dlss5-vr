# Dependencies (`deps/`)

Pre-compiled runtime binaries bundled with DLSS 5 Neural Rendering VR.

- **`OptiScaler.dll`** - modified DLSS-NR Pre-SR OptiScaler build with the
  live VR control channel. **GPL-3.0-or-later**; source and patch in
  [`optiscaler-src/`](optiscaler-src/README.md).
- **`OptiScaler.ini`** - default engine configuration (VR-tuned).
- **`cudart64_12.dll`** - NVIDIA CUDA 12 runtime, required by
  `nvngx_dlssnr.dll`; redistributed under the CUDA EULA.
- **`LICENSES/`** - full third-party license texts.

Not bundled (fetched by the installer from the upstream
[wilsjo2 release](https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass/releases/tag/v0.7.6)
when missing): the `nvngx.dll_dlssnr.dll` signature forwarder and the
`OptiScaler/` backend folder (XeSS/FidelityFX/Agility SDK runtimes and the
NVFP4 hybrid kernels, which are derived from NVIDIA model data).

The NVIDIA DLSS 5 runtime (`nvngx_dlssnr.dll`) is provided by the user and is
never redistributed or downloaded here.

The NVIDIA DLSS 5 runtime (`nvngx_dlssnr.dll`) is provided by the user and is
never redistributed here.
