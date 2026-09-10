#!/usr/bin/env python3
"""
Live probe for the DLSS5-VR proxy.

Inspects the running Cyberpunk 2077 process and its proxy log in one command:
  - module bases (proxy, OptiScaler, RealVR64, XInput, HID)
  - current XInput entry chains (who intercepts x9/x14)
  - game IAT slot for XInputGetState
  - shared-memory control block state (seq/ack/ready/values)
  - tail of the proxy log, filtered on the diagnostic markers

No game restart needed: use it after a single launch to see what the hooks see.
Use --follow to keep printing new diagnostic lines.
"""

import argparse
import ctypes
import ctypes.wintypes as w
import struct
import sys
import time
from pathlib import Path

PROCESS_VM_READ = 0x10
PROCESS_QUERY_INFORMATION = 0x400
TH32CS_SNAPMODULE = 0x8
TH32CS_SNAPMODULE32 = 0x10
FILE_MAP_READ = 0x4

GAME_EXE = "Cyberpunk2077.exe"
DEFAULT_LOG = r"C:\Program Files (x86)\Steam\steamapps\common\Cyberpunk 2077\bin\x64\vr_dlss5_proxy.log"


class MODULEENTRY32(ctypes.Structure):
    _fields_ = [
        ("dwSize", w.DWORD), ("th32ModuleID", w.DWORD), ("th32ProcessID", w.DWORD),
        ("GlblcntUsage", w.DWORD), ("ProccntUsage", w.DWORD),
        ("modBaseAddr", ctypes.c_void_p), ("modBaseSize", w.DWORD), ("hModule", w.HMODULE),
        ("szModule", ctypes.c_char * 256), ("szExePath", ctypes.c_char * 260),
    ]


def open_process(name):
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.CreateToolhelp32Snapshot.restype = w.HANDLE
    k.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    k.Process32First.argtypes = [w.HANDLE, ctypes.c_void_p]
    k.Process32Next.argtypes = [w.HANDLE, ctypes.c_void_p]
    k.OpenProcess.restype = w.HANDLE
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]

    class PROCESSENTRY32(ctypes.Structure):
        _fields_ = [
            ("dwSize", w.DWORD), ("cntUsage", w.DWORD), ("th32ProcessID", w.DWORD),
            ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", w.DWORD),
            ("cntThreads", w.DWORD), ("th32ParentProcessID", w.DWORD),
            ("pcPriClassBase", ctypes.c_long), ("dwFlags", w.DWORD),
            ("szExeFile", ctypes.c_char * 260),
        ]

    snap = k.CreateToolhelp32Snapshot(0x2, 0)
    entry = PROCESSENTRY32()
    entry.dwSize = ctypes.sizeof(PROCESSENTRY32)
    if k.Process32First(snap, ctypes.byref(entry)):
        while True:
            if entry.szExeFile.decode().lower() == name.lower():
                h = k.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, entry.th32ProcessID)
                return k, h, entry.th32ProcessID
            if not k.Process32Next(snap, ctypes.byref(entry)):
                break
    return k, None, None


def modules(k, pid):
    snap = k.CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)
    entry = MODULEENTRY32()
    entry.dwSize = ctypes.sizeof(MODULEENTRY32)
    out = {}
    if k.Module32First(snap, ctypes.byref(entry)):
        while True:
            out[entry.szModule.decode().lower()] = (entry.modBaseAddr, entry.modBaseSize)
            if not k.Module32Next(snap, ctypes.byref(entry)):
                break
    return out


def reader(k, h):
    k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p,
                                    ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    k.ReadProcessMemory.restype = w.BOOL

    def rd(addr, size):
        buf = ctypes.create_string_buffer(size)
        read = ctypes.c_size_t(0)
        if k.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(read)):
            return buf.raw[: read.value]
        return b""

    return rd


def owner_of(mods, addr):
    for name, (base, size) in mods.items():
        if base and base <= addr < base + size:
            return name
    return "?"


def follow(rd, mods, addr, label, depth=5):
    cur = addr
    for hop in range(depth):
        b = rd(cur, 16)
        if len(b) >= 5 and b[0] == 0xE9:
            rel = struct.unpack_from("<i", b, 1)[0]
            nxt = cur + 5 + rel
            print(f"  {label}[{hop}] {cur:#x} E9 -> {nxt:#x} ({owner_of(mods, nxt)})")
            cur = nxt
        elif len(b) >= 6 and b[0] == 0xFF and b[1] == 0x25:
            disp = struct.unpack_from("<i", b, 2)[0]
            slot = cur + 6 + disp
            p = rd(slot, 8)
            if len(p) != 8:
                break
            tgt = struct.unpack("<Q", p)[0]
            print(f"  {label}[{hop}] {cur:#x} FF25 -> [{slot:#x}]={tgt:#x} ({owner_of(mods, tgt)})")
            cur = tgt
        else:
            print(f"  {label}[{hop}] {cur:#x} bytes={b[:10].hex()} ({owner_of(mods, cur)})")
            break


def control_block(pid):
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.OpenFileMappingA.restype = w.HANDLE
    k.OpenFileMappingA.argtypes = [w.DWORD, w.BOOL, ctypes.c_char_p]
    k.MapViewOfFile.restype = ctypes.c_void_p
    k.MapViewOfFile.argtypes = [w.HANDLE, w.DWORD, w.DWORD, w.DWORD, ctypes.c_size_t]
    name = f"Local\\VRDLSS5_Control_1_{pid}".encode()
    handle = k.OpenFileMappingA(FILE_MAP_READ, False, name)
    if not handle:
        print("control block: not found (OptiScaler proxy not up yet?)")
        return
    view = k.MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0x80)
    if not view:
        print("control block: open failed")
        return
    b = ctypes.string_at(view, 0x80)
    magic, ver, seq, ack, ready, enabled = struct.unpack_from("<IIllll", b, 0)
    ws = struct.unpack_from("<f", b, 24)[0]
    run_before, residual, preset = struct.unpack_from("<lll", b, 28)
    intensity = struct.unpack_from("<f", b, 40)[0]
    style = struct.unpack_from("<l", b, 44)[0]
    print(f"control block: magic={magic:#x} ver={ver} seq={seq} ack={ack} ready={ready}")
    print(f"  enabled={enabled} workingScale={ws:.2f} preSR={run_before} residualRR={residual} "
          f"preset={preset} intensity={intensity:.2f} style={style}")


def tail_log(path, lines, marker):
    p = Path(path)
    if not p.is_file():
        print(f"log not found: {path}")
        return
    content = p.read_text(encoding="utf-8", errors="replace").splitlines()
    if marker:
        content = [l for l in content if marker in l]
    for line in content[-lines:]:
        print(line)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--log", default=DEFAULT_LOG)
    ap.add_argument("--tail", type=int, default=25)
    ap.add_argument("--filter", default="Proxy-Diag|thunk calls|RawInput|HID device|HID raw|hat switch|control applied|DLSS-NR running")
    ap.add_argument("--follow", action="store_true")
    args = ap.parse_args()

    k, h, pid = open_process(GAME_EXE)
    if not h:
        print(f"{GAME_EXE} is not running.")
        return 1

    print(f"PID {pid}")
    mods = modules(k, pid)
    for name in ("dxgi.dll", "optiscaler.asi", "realvr64.dll", "xinput1_4.dll",
                 "xinput9_1_0.dll", "hid.dll", "user32.dll"):
        if name in mods:
            print(f"  {name:18s} base={mods[name][0]:#x} size={mods[name][1]:#x}")

    rd = reader(k, h)

    def proc_addr(module, export):
        k.LoadLibraryA.restype = w.HMODULE
        k.LoadLibraryA.argtypes = [ctypes.c_char_p]
        k.GetProcAddress.restype = ctypes.c_void_p
        k.GetProcAddress.argtypes = [w.HMODULE, ctypes.c_char_p]
        k.GetModuleHandleA.restype = w.HMODULE
        k.GetModuleHandleA.argtypes = [ctypes.c_char_p]
        local = k.LoadLibraryA(module.encode())
        fn = k.GetProcAddress(local, export.encode())
        base = k.GetModuleHandleA(module.encode())
        if not fn or module not in mods:
            return 0
        return mods[module][0] + (fn - base)

    print("XINPUT9_1_0!XInputGetState chain:")
    follow(rd, mods, proc_addr("xinput9_1_0.dll", "XInputGetState"), "x9")
    print("XINPUT1_4!XInputGetState chain:")
    follow(rd, mods, proc_addr("xinput1_4.dll", "XInputGetState"), "x14")

    # Game IAT slot for xinput9_1_0!XInputGetState (RVA is stable for this exe).
    exe = mods.get("cyberpunk2077.exe") or mods.get("cyberpunk2077")
    if exe:
        iat = rd(exe[0] + 0x2A8DDD0, 8)
        if len(iat) == 8:
            tgt = struct.unpack("<Q", iat)[0]
            print(f"game IAT[XInputGetState] = {tgt:#x} ({owner_of(mods, tgt)})")

    control_block(pid)

    print(f"--- log ({args.log}) ---")
    if args.follow:
        seen = 0
        while True:
            try:
                content = Path(args.log).read_text(encoding="utf-8", errors="replace").splitlines()
            except OSError:
                content = []
            hits = [l for l in content if not args.filter or any(t in l for t in args.filter.split("|"))]
            for line in hits[seen:]:
                print(line)
            seen = len(hits)
            time.sleep(1)
    else:
        tail_log(args.log, args.tail, args.filter)
    return 0


if __name__ == "__main__":
    sys.exit(main())
