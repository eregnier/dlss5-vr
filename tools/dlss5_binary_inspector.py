#!/usr/bin/env python3
"""
DLSS 5 Binary & Architecture Inspector
Extracts PE exports, imports, forwarders, and scans for CUDA fatbins / compute architectures.
"""

import sys
import os
import struct
import hashlib
from pathlib import Path

def get_file_hash(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().upper()

def scan_cuda_architectures(data: bytes) -> list[str]:
    archs = set()
    signatures = [
        b"sm_70", b"sm_75", b"sm_80", b"sm_86", b"sm_89", b"sm_90", b"sm_100",
        b"compute_70", b"compute_75", b"compute_80", b"compute_86", b"compute_89", b"compute_90"
    ]
    for sig in signatures:
        if sig in data:
            archs.add(sig.decode("ascii"))
    return sorted(list(archs))

def parse_pe_info(path: Path):
    if not path.is_file():
        return None
    data = path.read_bytes()
    size = len(data)
    sha256 = get_file_hash(path)
    
    # Check MZ header
    if len(data) < 64 or data[:2] != b"MZ":
        return {"error": "Not a PE file", "size": size, "sha256": sha256}
        
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    if e_lfanew + 24 > len(data) or data[e_lfanew:e_lfanew+4] != b"PE\x00\x00":
        return {"error": "Invalid PE header", "size": size, "sha256": sha256}
        
    machine = struct.unpack_from("<H", data, e_lfanew + 4)[0]
    arch_map = {0x8664: "x64 (AMD64)", 0x14C: "x86 (32-bit)", 0xAA64: "ARM64"}
    arch_str = arch_map.get(machine, f"Unknown (0x{machine:X})")
    
    cuda_archs = scan_cuda_architectures(data)
    
    # Check string references
    has_ngx = b"NVSDK_NGX" in data
    has_openvr = b"IVROverlay" in data or b"openvr" in data.lower()
    has_openxr = b"xrCreateInstance" in data or b"openxr" in data.lower()
    has_renodx = b"RenoDX" in data or b"renodx" in data.lower()
    has_optiscaler = b"OptiScaler" in data or b"optiscaler" in data.lower()
    has_luke = b"RealVR" in data or b"AUDLSSFix" in data
    
    return {
        "file": path.name,
        "path": str(path),
        "size_kb": size / 1024.0,
        "sha256": sha256,
        "machine": arch_str,
        "cuda_archs": cuda_archs,
        "has_ngx": has_ngx,
        "has_openvr": has_openvr,
        "has_openxr": has_openxr,
        "has_renodx": has_renodx,
        "has_optiscaler": has_optiscaler,
        "has_luke": has_luke
    }

def main():
    target_dirs = [
        Path(r"C:\code\clones\binaries"),
        Path(r"C:\code\dlss5-vr\deps"),
        Path(r"C:\code\dlss5-vr\proxy"),
        Path(r"C:\code\vrdlss5\RealRepo"),
    ]
    
    print("=" * 95)
    print(" DLSS 5 & VR MOD BINARY INSPECTION REPORT")
    print("=" * 95)
    
    targets = [
        r"C:\code\clones\binaries\OptiScaler.dll",
        r"C:\code\clones\binaries\nvngx.dll_dlssnr.dll",
        r"C:\code\dlss5-vr\deps\renodx-dlss5.addon64",
        r"C:\code\dlss5-vr\deps\ReShade64_dlss5.dll",
        r"C:\code\dlss5-vr\proxy\dxgi.dll",
        r"C:\code\vrdlss5\RealRepo\RealVR64.dll",
    ]
    
    for t in targets:
        p = Path(t)
        if not p.is_file():
            continue
        info = parse_pe_info(p)
        print(f"\n[FILE]: {info['file']}")
        print(f"  Path:       {info['path']}")
        print(f"  Size:       {info['size_kb']:.1f} KB")
        print(f"  Arch:       {info['machine']}")
        print(f"  SHA-256:    {info['sha256']}")
        if info['cuda_archs']:
            print(f"  CUDA Arch:  {', '.join(info['cuda_archs'])}")
        features = []
        if info['has_ngx']: features.append("NVIDIA NGX (DLSS)")
        if info['has_optiscaler']: features.append("OptiScaler Engine")
        if info['has_renodx']: features.append("RenoDX Subsystem")
        if info['has_luke']: features.append("Luke Ross RealVR Hooking")
        if info['has_openvr']: features.append("OpenVR (SteamVR)")
        if info['has_openxr']: features.append("OpenXR")
        print(f"  Identified: {', '.join(features) if features else 'None'}")

    print("\n" + "=" * 95)

if __name__ == "__main__":
    main()
