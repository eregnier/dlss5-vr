import os
import sys
import re

def scan_strings(path, term):
    with open(path, 'rb') as f:
        data = f.read()
    u8 = term.encode('utf-8')
    u16 = term.encode('utf-16le')
    p8 = [i for i in range(len(data)) if data.startswith(u8, i)]
    p16 = [i for i in range(len(data)) if data.startswith(u16, i)]
    print(term + ' in ' + os.path.basename(path) + ': ascii=' + str(len(p8)) + ', wide=' + str(len(p16)))
    for p in p16:
        print('  wide @ ' + hex(p) + ': ' + repr(data[max(0, p-10):min(len(data), p+len(u16)+10)]))

def dump_hook_table(path):
    with open(path, 'rb') as f:
        data = f.read()
    chunk = data[0x41c600:0x41cb00]
    print('Hook table entries in ' + os.path.basename(path) + ':')
    for m in re.finditer(rb'(?:[a-zA-Z0-9_\-\.]\x00){3,}', chunk):
        s = m.group(0).decode('utf-16le', 'ignore')
        if '.dll' in s.lower():
            print('  ' + s)

def patch_wide_exact(path, old_s, new_s):
    assert len(old_s) == len(new_s)
    with open(path, 'rb') as f:
        data = f.read()
    orig_len = len(data)
    o16 = old_s.encode('utf-16le')
    n16 = new_s.encode('utf-16le')
    count = data.count(o16)
    if count == 0:
        print('Pattern ' + old_s + ' not found')
        return
    data = data.replace(o16, n16)
    assert len(data) == orig_len
    with open(path, 'wb') as f:
        f.write(data)
    print('Replaced ' + str(count) + ' instances of ' + old_s + ' -> ' + new_s + ' in ' + os.path.basename(path))

def patch_bytes(path, offset_hex, old_hex, new_hex):
    offset = int(offset_hex, 16)
    old_b = bytes.fromhex(old_hex)
    new_b = bytes.fromhex(new_hex)
    assert len(old_b) == len(new_b), "Lengths must match!"
    with open(path, 'rb') as f:
        data = bytearray(f.read())
    assert data[offset:offset+len(old_b)] == old_b, "Mismatch at offset!"
    data[offset:offset+len(new_b)] = new_b
    with open(path, 'wb') as f:
        f.write(data)
    print("Successfully patched bytes at " + hex(offset) + " in " + os.path.basename(path))

if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'scan':
        scan_strings(sys.argv[2], sys.argv[3])
    elif cmd == 'patch':
        patch_wide_exact(sys.argv[2], sys.argv[3], sys.argv[4])
    elif cmd == 'dumphooks':
        dump_hook_table(sys.argv[2])
    elif cmd == 'patchbytes':
        patch_bytes(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    elif cmd == 'compile':
        import subprocess
        vcvars = r"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
        proxy_dir = r"C:\code\vrdlss5\proxy"
        compile_cmd = f'call "{vcvars}" && cd /d "{proxy_dir}" && cl.exe /O2 /LD /Fe:dxgi.dll proxy.cpp /link /DEF:proxy.def /SUBSYSTEM:WINDOWS'
        print("Compiling proxy...")
        res = subprocess.run(compile_cmd, shell=True, capture_output=True, text=True)
        print(res.stdout)
        if res.stderr:
            print("STDERR:", res.stderr)
        print("Exit code:", res.returncode)
    elif cmd == 'trace':
        with open(sys.argv[2], 'rb') as f:
            d = f.read()
        for i in range(0xdf00, 0xe250):
            if d[i:i+2] == b'\x0f\x84':
                disp = int.from_bytes(d[i+2:i+6], 'little', signed=True)
                print(hex(i) + ': JE ' + hex(i + 6 + disp))
            elif d[i:i+2] == b'\x0f\x85':
                disp = int.from_bytes(d[i+2:i+6], 'little', signed=True)
                print(hex(i) + ': JNE ' + hex(i + 6 + disp))
            elif d[i] == 0xe9:
                disp = int.from_bytes(d[i+1:i+5], 'little', signed=True)
                print(hex(i) + ': JMP ' + hex(i + 5 + disp))
    elif cmd == 'map_eval':
        import struct
        addon_path = sys.argv[2]
        out_path = sys.argv[3]
        with open(addon_path, 'rb') as f:
            data = f.read()
        pe_offset = struct.unpack('<I', data[0x3c:0x40])[0]
        opt_header = pe_offset + 24
        magic = struct.unpack('<H', data[opt_header:opt_header+2])[0]
        num_sections = struct.unpack('<H', data[pe_offset+6:pe_offset+8])[0]
        sect_offset = opt_header + (240 if magic == 0x20b else 224)
        sections = []
        for _ in range(num_sections):
            name = data[sect_offset:sect_offset+8].rstrip(b'\x00').decode('latin1')
            vsize, vrva, rsize, rraw = struct.unpack('<IIII', data[sect_offset+8:sect_offset+24])
            sections.append((name, vrva, vsize, rraw, rsize))
            sect_offset += 40
        def file_to_rva(f_off):
            for name, vrva, vsize, rraw, rsize in sections:
                if rraw <= f_off < rraw + rsize: return vrva + (f_off - rraw)
            return 0
        lines = []
        lines.append("=== CARTOGRAPHIE DU PIPELINE EVALUATION RENODX ===")
        targets = [
            b"inline feature 18 evaluation succeeded",
            b"feature 18 created via",
            b"feature 18 evaluate failed",
            b"not in registry",
            b"skipping inline NR",
            b"first NGX evaluate intercepted"
        ]
        for t in targets:
            pos = 0
            while True:
                idx = data.find(t, pos)
                if idx == -1: break
                rva = file_to_rva(idx)
                lines.append(f"STRING: '{t.decode('latin1')}' file=0x{idx:X} RVA=0x{rva:X}")
                for s_name, vrva, vsize, rraw, rsize in sections:
                    if s_name != '.text': continue
                    sec_data = data[rraw:rraw+rsize]
                    for j in range(len(sec_data) - 4):
                        disp = int.from_bytes(sec_data[j:j+4], 'little', signed=True)
                        if vrva + j + 4 + disp == rva:
                            xref = rraw + j
                            pre = data[max(0, xref-3):xref]
                            lines.append(f"  -> XREF file=0x{xref:X} RVA=0x{vrva+j:X} pre={pre.hex()}")
                pos = idx + len(t)
        lines.append("\n=== SAUTS DANS HOOK EVALUATE (0x376C0 - 0x38000) ===")
        i = 0x376C0
        while i < 0x38000:
            b1 = data[i]
            b2 = data[i+1] if i+1 < len(data) else 0
            if 0x70 <= b1 <= 0x7F:
                disp8 = int.from_bytes(data[i+1:i+2], 'little', signed=True)
                lines.append(f"0x{i:06X}: Jcc short -> 0x{i+2+disp8:06X} ({data[i:i+2].hex()})")
                i += 2; continue
            if b1 == 0x0F and 0x80 <= b2 <= 0x8F:
                disp32 = int.from_bytes(data[i+2:i+6], 'little', signed=True)
                lines.append(f"0x{i:06X}: Jcc near  -> 0x{i+6+disp32:06X} ({data[i:i+6].hex()})")
                i += 6; continue
            if b1 == 0xE9:
                disp32 = int.from_bytes(data[i+1:i+5], 'little', signed=True)
                lines.append(f"0x{i:06X}: JMP near  -> 0x{i+5+disp32:06X} ({data[i:i+5].hex()})")
                i += 5; continue
            i += 1
        lines.append("\n=== SAUTS DANS INJECTION FEATURE 18 (0x09F00 - 0x0A500) ===")
        i = 0x09F00
        while i < 0x0A500:
            b1 = data[i]
            b2 = data[i+1] if i+1 < len(data) else 0
            if 0x70 <= b1 <= 0x7F:
                disp8 = int.from_bytes(data[i+1:i+2], 'little', signed=True)
                lines.append(f"0x{i:06X}: Jcc short -> 0x{i+2+disp8:06X} ({data[i:i+2].hex()})")
                i += 2; continue
            if b1 == 0x0F and 0x80 <= b2 <= 0x8F:
                disp32 = int.from_bytes(data[i+2:i+6], 'little', signed=True)
                lines.append(f"0x{i:06X}: Jcc near  -> 0x{i+6+disp32:06X} ({data[i:i+6].hex()})")
                i += 6; continue
            i += 1
        with open(out_path, 'w', encoding='utf-8') as out_f:
            out_f.write('\n'.join(lines))
        print(f"Rapport ecrit : {out_path} ({len(lines)} lignes)")