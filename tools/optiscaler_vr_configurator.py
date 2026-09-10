#!/usr/bin/env python3
"""
OptiScaler VR Configuration Optimizer & Validator
Tailored for Luke Ross REAL VR (Cyberpunk 2077, Star Wars Outlaws, etc.)
Configures OptiScaler.ini for Pre-SR multipass, WorkingScale, and Ray Reconstruction.
"""

import sys
import os
import re
from pathlib import Path

def optimize_optiscaler_vr_ini(
    ini_path: Path,
    working_scale: float = 0.75,
    run_before_sr: bool = True,
    passes: int = 1,
    residual_across_rr: bool = True,
    residual_blend: float = 0.08,
    enable_fg: bool = False,
    output_path: Path = None
) -> str:
    """
    Reads or creates an OptiScaler.ini and applies the high-performance VR settings.
    """
    if ini_path.is_file():
        content = ini_path.read_text(encoding="utf-8", errors="replace")
    else:
        content = "[DlssNr]\n"

    # Settings dictionary for [DlssNr]
    nr_settings = {
        "Enabled": "true",
        "RunBeforeSR": "true" if run_before_sr else "false",
        "WorkingScale": f"{working_scale:.2f}",
        "Passes": str(passes),
        "ResidualAcrossRR": "true" if residual_across_rr else "false",
        "ResidualAcrossRRBlend": f"{residual_blend:.2f}",
        "DeferredDLSS": "false",  # Keep false unless explicitly running private DLSS upscale
        "ResidualFG": "false",
        "AutoCapture": "false",   # Avoid disk I/O during gameplay
        "DebugView": "0",
    }
    
    # Apply to INI
    lines = content.splitlines()
    in_section = False
    section_found = False
    modified_keys = set()
    new_lines = []
    
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            sec_name = stripped[1:-1].strip()
            if in_section:
                # Add any missing keys from DlssNr before leaving section
                for k, v in nr_settings.items():
                    if k not in modified_keys:
                        new_lines.append(f"{k}={v}")
                in_section = False
            if sec_name.lower() == "dlssnr":
                in_section = True
                section_found = True
                new_lines.append("[DlssNr]")
                continue
                
        if in_section:
            if "=" in stripped and not stripped.startswith(";") and not stripped.startswith("#"):
                key = stripped.split("=", 1)[0].strip()
                if key in nr_settings:
                    new_lines.append(f"{key}={nr_settings[key]}")
                    modified_keys.add(key)
                    continue
        new_lines.append(line)
        
    if not section_found:
        new_lines.append("\n[DlssNr]")
        for k, v in nr_settings.items():
            new_lines.append(f"{k}={v}")
    elif in_section:
        for k, v in nr_settings.items():
            if k not in modified_keys:
                new_lines.append(f"{k}={v}")

    result = "\n".join(new_lines) + "\n"
    
    out_file = output_path or ini_path
    out_file.parent.mkdir(parents=True, exist_ok=True)
    out_file.write_text(result, encoding="utf-8")
    return result

def main():
    template_src = Path(r"C:\code\clones\binaries\OptiScaler.ini")
    target_out = Path(r"C:\code\dlss5-vr\tools\OptiScaler-VR-Cyberpunk2077.ini")
    
    print(f"Generating tuned VR configuration from {template_src}...")
    optimize_optiscaler_vr_ini(template_src, working_scale=0.75, run_before_sr=True, passes=1, residual_across_rr=True, output_path=target_out)
    print(f"Saved optimized VR configuration to: {target_out}")

if __name__ == "__main__":
    main()
