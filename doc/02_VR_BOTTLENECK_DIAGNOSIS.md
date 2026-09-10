# 02. In-Depth Diagnosis of VR Bottlenecks (Cyberpunk 2077 & Luke Ross)

This document presents the mathematical, hardware and software analysis behind the brutal performance drop (72 FPS $\rightarrow$ 40 FPS, GPU 70% $\rightarrow$ 100%) when enabling DLSS 5 in virtual reality.

---

## 1. The Mathematical Reality of High-Resolution VR Rendering

### A. Pixel Surface: Flat 4K vs Stereo VR
On a traditional flat screen, 4K Ultra-HD rendering represents:
$$S_{\text{flat 4K}} = 3840 \times 2160 = \mathbf{8\,294\,400\text{ pixels}}$$

In a high-fidelity VR headset (supersampled Meta Quest 3 or native Pimax Crystal):
- Target resolution per eye: $\sim 3840 \times 3840$ pixels.
- Surface per eye: $3840 \times 3840 = 14\,745\,600\text{ pixels}$.
- **Total stereoscopic surface per image**:
  $$S_{\text{VR total}} = 14\,745\,600 \times 2 = \mathbf{29\,491\,200\text{ pixels}}$$

The VR graphics pipeline processes **3.56× more pixels** than standard flat 4K. At 72 Hz, the GPU must generate and post-process more than **2.12 billion pixels every second**.

---

## 2. The Mechanics of the VR V-Sync Cliff: The "Cliff" Effect

### A. The Strict Time Budget
Unlike computer monitors with G-Sync / FreeSync where the refresh adapts frame by frame, a VR headset requires strict synchronization with its micro-display panels to avoid motion sickness.

| Headset rate | Strict V-Sync budget ($\Delta t_{\text{budget}}$) | Tolerance threshold |
| :--- | :--- | :--- |
| **72 Hz** (Quest 3 / Pimax standard) | **13.88 ms** | $> 13.88\text{ ms} \rightarrow$ drop to **36 FPS** |
| **80 Hz** | **12.50 ms** | $> 12.50\text{ ms} \rightarrow$ drop to **40 FPS** |
| **90 Hz** (Pimax Crystal standard) | **11.11 ms** | $> 11.11\text{ ms} \rightarrow$ drop to **45 FPS** |
| **120 Hz** | **8.33 ms** | $> 8.33\text{ ms} \rightarrow$ drop to **60 FPS** |

### B. Why 40 FPS and 100% GPU?
Consider the real measurements taken on an **NVIDIA GeForce RTX 5090** in Cyberpunk 2077 with the LukeRoss mod (DLSS Performance):

1. **Base game rendering (Cyberpunk 2077)**:
   - Base GPU frame time: $\sim 9.7\text{ ms}$.
   - GPU load: $\frac{9.7}{13.88} \approx \mathbf{70\%}$.
   - Remaining headroom: $13.88 - 9.7 = \mathbf{4.18\text{ ms}}$.
   - **Result: perfect, stable 72 FPS.**

2. **Enabling DLSS 5 through RenoDX (monolithic Post-SR)**:
   - The `nvngx_dlssnr.dll` model weighs 165 MB and contains millions of FP16/FP8 weights.
   - Tensor Core inference time to process 29.49 million pixels is:
     $$T_{\text{Tensor}} \approx \mathbf{12.0\text{ ms}}$$
   - **Total time required per frame**:
     $$T_{\text{total}} = 9.7\text{ ms (engine)} + 12.0\text{ ms (DLSS 5)} = \mathbf{21.7\text{ ms}}$$

3. **Asynchronous reprojection kicks in (ASW / Motion Smoothing)**:
   - $21.7\text{ ms} \gg 13.88\text{ ms}$. The engine systematically misses the V-Sync window.
   - The VR runtime (Oculus / SteamVR) immediately locks the display rate to **half the native rate**:
     $$72\text{ Hz} \longrightarrow \mathbf{36\text{ to }40\text{ FPS}}$$
   - For every real frame computed late (taking 21.7 ms), the compositor injects a frame synthesized through spatio-temporal reprojection.
   - The GPU runs at 100% because it is constantly late and trying to catch up with the next frame train.
   - **The sawtooth effect**: load alternates between 100% (late frame) and 50% (waiting for the compositor's next pose), creating an unpleasant perceived instability.

---

## 3. The Internal Bottlenecks Identified at Binary Level

Besides the raw pixel volume, the binary and memory audit identified several critical weaknesses in the old RenoDX + proxy implementation:

### A. The Slot 0 Trap (Direct3D 12 UAV Hazard)
- In the disassembled code of `renodx-dlss5.addon64` at offset `0xDF64`, the old emergency patch forced assignment to `Slot 0` scratch memory.
- In VR both eyes are submitted consecutively: the left eye wrote to Slot 0, and the right eye overwrote Slot 0's UAVs a few microseconds later.
- The NVIDIA D3D12 driver detected a concurrent write collision with no memory barrier and triggered a full **GPU Pipeline Flush** (`ExecuteCommandLists` serialization), destroying all compute parallelism.

### B. Inter-Queue Serialization (`CommandQueue->Wait`)
- At offset `0x31150`, RenoDX waited on the previous submission's fence.
- In a multi-queue VR setup (LukeRoss stereoscopic submission), that mutual synchronization blocked the GPU render thread until the other eye finished completely, preventing render overlap.

### C. Synchronous Disk Access on the Hot Path
- The old proxy performed synchronous `WritePrivateProfileString` / `GetPrivateProfileString` reads and writes directly on the render thread at 144 Hz (72 Hz × 2 eyes). Each disk access blocked the CPU for 0.5 to 2 ms, enough to push the frame past the V-Sync budget.

### D. The `NRPreset` vs `NRStyle` Register Collision
- In RenoDX's internal structure, address `0x196B98` drives the network architecture (`NRPreset`), while `0x196C2C` drives the image tone (`NRStyle`).
- Writing both at once violently changed cinematic tone mapping while letting the user believe the AI cost barely varied.
- Moreover, NGX only takes neural network presets into account **when the feature is created**. A hot change in-game had no effect without explicitly re-creating the resource.

---

## 4. Conclusion & Architectural Solution

Everything converges on one unambiguous conclusion:
- **It is physically and mathematically impossible to run a heavy denoising/reconstruction neural network (165 MB) in Post-SR at 4K per eye (29.5 Mpx) within a 13.88 ms budget.**
- **The only viable method is to run the neural network in Pre-SR**, that is, **on the low-resolution render buffer (1920 × 1920 or lower via `WorkingScale`)**, exactly what the `wilsjo2/OptiScaler-DLSSNR-PreSR-Multipass` architecture does.
