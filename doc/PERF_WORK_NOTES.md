# Performance Work Notes (Archive)

> **[Archive]** These notes come from the RenoDX add-on era (branch `perf`). The current pipeline uses the OptiScaler Pre-SR engine and is documented in [05_DLSS5_VR_ARCHITECTURE.md](05_DLSS5_VR_ARCHITECTURE.md). Kept here for the measurements and the VR-specific reasoning.

This document details the performance diagnosis performed when enabling DLSS 5 in VR on a GeForce RTX 5090: hardware, D3D12, software and pipeline bottlenecks, and the optimizations implemented during that phase.

---

## 1. Context & Reported Symptoms

### Clinical observation
- **Setup**: GeForce RTX 5090, high-resolution VR headset (supersampled Quest 3 / Pimax Crystal at ~4K per eye), Cyberpunk 2077 with the LukeRoss REAL VR mod.
- **Without DLSS 5 (standard DLSS 4.5)**: perfectly locked 72 FPS, GPU load steady at **~70%**.
- **With DLSS 5 (Neural Reconstruction runtime)**: immediate collapse to **~40 FPS**, GPU pegged at **99%**, with a **sawtooth load curve**.

---

## 2. In-Depth Diagnosis & Mathematical Analysis

### A. The mathematical wall of per-eye 4K in VR
- **Flat 4K (3840 × 2160)**: 8.29 million pixels per image.
- **VR 4K per eye (~3840 × 3840 per eye)**: 14.75 million pixels per eye, i.e. **29.5 million pixels** per stereo pair.
- **Conclusion**: VR 4K requires processing **3.5× more pixels per second** than standard 4K. At 72 Hz, the GPU must process and denoise more than **2.12 billion pixels per second**.

### B. The critical time budget at 72 Hz
At 72 Hz, the V-Sync interval is strictly capped:
$$\Delta t_{\text{max}} = \frac{1000\text{ ms}}{72} \approx \mathbf{13.88\text{ ms}}$$

1. **Base game (DLSS 4.5)**:
   - GPU frame time: $\sim 9.7\text{ ms}$ (i.e. 70% of the 5090).
   - Available GPU headroom: $13.88 - 9.7 = \mathbf{4.18\text{ ms}}$.
2. **DLSS 5 neural inference (`nvngx_dlssnr.dll`)**:
   - Heavy 165 MB model with FP16/FP8 weights (versus ~35 MB for standard DLSS).
   - Tensor Core convolution time per eye at 4K: $\sim 5.5\text{ ms}$ to $7\text{ ms}$.
   - Combined stereo cost: $11\text{ ms}$ to $14\text{ ms}$.
3. **Total GPU time**:
   $$\text{Frame Time} = 9.7\text{ ms (Cyberpunk)} + 12\text{ ms (DLSS 5)} = \mathbf{21.7\text{ ms}}$$
4. **The cliff and the "sawtooth"**:
   - As soon as the frame time exceeds 13.88 ms, the VR compositor (SteamVR / Oculus) misses the V-Sync window.
   - Asynchronous reprojection (ASW / Motion Smoothing) immediately halves the rate ($72 \rightarrow 36\text{-}40\text{ fps}$).
   - **Origin of the sawtooth**: the GPU alternates between a late frame computed at full load and a reprojected synthetic frame where the pipeline waits for the next pose, causing a constant 99% ↔ load-drop oscillation.

---

## 3. Root Causes Identified (RenoDX era)

### Cause 1: scratch-slot contention between the two eyes
The emergency patch in use forced every evaluation into the same scratch slot. In VR both eyes are submitted a few microseconds apart, so the right eye overwrote the left eye's UAVs. The NVIDIA D3D12 driver detected a concurrent write collision without a barrier and forced a full **GPU pipeline flush** (`ExecuteCommandLists` serialization) — the direct accelerator of sawtooth and stutter.

### Cause 2: inter-queue temporal serialization
When one eye's queue had not signalled its fence yet, the add-on inserted a `CommandQueue->Wait()`, blocking the other queue on the GPU. In a multi-queue VR setup this artificially serialized both eyes' rendering.

### Cause 3: synchronous work on the render thread
The old proxy performed synchronous disk I/O (`fopen`/`fprintf`/`fflush`/`fclose` logging, repeated `WritePrivateProfileString` calls) and per-frame float math directly on the render thread invoked at 144 Hz (72 Hz × 2 eyes). Each disk access blocked the CPU for 0.5 to 2 ms.

### Cause 4: Tensor Core overprovisioning
The default neural preset was the heaviest one. The performance preset uses a lighter topology, cutting Tensor inference time by 20-30% with no perceptible fidelity loss in moving VR content.

---

## 4. Optimizations Implemented During That Phase (Historical)

1. **Lock-free circular scratch ring (4 slots)** so left/right eyes and successive frames never share a UAV generation, removing the pipeline flushes.
2. **Inter-queue wait bypass** so the two eyes' submissions no longer wait on each other.
3. **Zero-overhead render hot path**: the NGX evaluate wrapper reduced to a counter increment + direct forward; all measurement moved to an asynchronous background thread.
4. **Single-pass INI serialization** instead of repeated read/parse/write cycles.
5. **GDI HUD rasterizer micro-optimizations** (branchless alpha, direct 32-bit writes).
6. **Performance preset by default** and decoupling of the network-architecture control from the tone-mapping control (they were previously written to the same field).
7. **Deferred process priority boost** (applied after boot, from a background thread).

Those changes were later superseded by the OptiScaler Pre-SR architecture, which removes the root problem (Post-SR 4K evaluation) instead of optimizing around it.

---

## 5. Why the Frame Rate Stayed at ~40 FPS Despite Preset Changes

### The VR reprojection cliff
On a flat screen, cutting frame time from 20 ms to 16 ms shows a linear FPS progression ($50 \rightarrow 62$). In VR the behaviour is discontinuous:
- If $\text{Frame Time} \le 13.88\text{ ms}$ (at 72 Hz) $\rightarrow$ **native 72 FPS**.
- If $\text{Frame Time} > 13.88\text{ ms}$ (14.5, 16 or 21 ms alike) $\rightarrow$ the VR compositor locks the rate to **half the refresh rate (36-40 FPS)** with synthesized reprojected frames.

### Explanation of the observed behaviour
1. Changing presets did vary Tensor time (from ~12 ms on the heavy preset to ~7 ms on the performance one).
2. But if the engine already takes **9.7 ms** for native per-eye 4K: $9.7 + 7 = \mathbf{16.7\text{ ms}} > 13.88\text{ ms}$ — still above the budget, so the compositor stayed at the reprojection step.
3. Additionally, the old field collision between network preset and tone style modified the color/HDR response on every preset change, making the visual difference obvious while the frame rate remained locked below the threshold.

---

## 6. User Configuration Recommendations (Historical, RenoDX era)

To cross the 13.88 ms bar with DLSS 5 active per-eye 4K:

| Setting | Location | Recommended value | Impact |
| :--- | :--- | :--- | :--- |
| **Cyberpunk DLSS mode** | Game graphics menu | **Performance** or **Balanced** | Restores base GPU render time to ~4.5 ms (instead of 9.7 ms native), providing headroom for the AI model |
| **DLSS 5 preset** | Old VR HUD (row 3) or `ReShade.ini` | **Preset 2 [Performance]** | Cuts Tensor Core time from 12 ms to ~6-7 ms |
| **LukeRoss stereo mode** | VR menu (Home key) | **AER v2 (Alternate Eye Rendering)** | Renders one eye per V-Sync cycle, halving per-frame inference GPU time |

With DLSS Performance (4.5 ms) + Preset 2 (6.5 ms) = **11 ms total GPU**, well below the 13.88 ms required to unlock full 72 FPS without reprojection.

> With the current OptiScaler Pre-SR pipeline, the equivalent goal is reached with `RunBeforeSR=true` and `WorkingScale=0.75` (~1.69 ms Tensor time), so heavy presets no longer need to be traded away for performance.

---

## 7. Results Summary (Historical)

- **UAV conflicts & sawtooth**: resolved via the lock-free 4-slot ring buffer.
- **GPU inter-queue serialization**: resolved via the wait bypass.
- **CPU render-path overhead**: reduced to near zero (counter + forward).
- **Preset/tone field collision**: decoupled and fixed.
- **Disk I/O**: removed from the render thread.
- **Compiler & binaries**: zero warnings, zero errors.
