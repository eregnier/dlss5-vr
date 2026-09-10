# OptiScaler Modified Source (GPL-3.0-or-later Compliance)

`deps/OptiScaler.dll` in this repository and in the release package is a
modified build of the DLSS-NR OptiScaler fork. OptiScaler is licensed under
the **GNU General Public License v3.0 or later** (see
`../LICENSES/GPL-3.0-or-later.txt`). In accordance with the GPL, the
corresponding source of the modified build is made available as:

1. **Upstream source** (unmodified base):
   https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass
   at commit `1b1dd650` (`v0.7.6-2-g1b1dd650`).

2. **Patch** with all changes made by this project:
   [`optiscaler-vrdlss5-control.patch`](optiscaler-vrdlss5-control.patch)

## Modifications

- `OptiScaler/Config.h`, `OptiScaler/Config.cpp`: live setting bridge
  (`VrDlss5Control`, shared memory `Local\VRDLSS5_Control_1_<pid>`) for
  Neural Engine toggle, WorkingScale, Pre-SR placement, residual across RR,
  preset and DLSS 5 Detail/Style.
- `OptiScaler/dllmain.cpp`: publish loop + version/magic handshake.
- `OptiScaler.ini`: VR-tuned defaults (Pre-SR enabled, WorkingScale 0.75,
  residual across RR). The modified file is in this directory as
  `OptiScaler.ini`; the code patch does not cover it.

## Rebuilding

```cmd
git clone https://github.com/wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass
cd OptiScaler-DLSSNR-PreSR-Multipass
git checkout 1b1dd650
git apply <path>\optiscaler-vrdlss5-control.patch
setup_windows.bat
```

Build `OptiScaler.sln` (Release, x64) with Visual Studio. The resulting
`OptiScaler.dll` replaces `deps/OptiScaler.dll` in this project.
