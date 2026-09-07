import os
import sys
import struct

def analyze_renodx(addon_path, out_report_path):
    with open(addon_path, 'rb') as f:
        data = f.read()

    report = []
    report.append(f'=== ANALYSE COMPLETE DU PIPELINE D EVALUATION RENODX ===')
    report.append(f'Fichier cible : {addon_path}')
    report.append(f'Taille binaire : {len(data)} octets\n')

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
            if rraw <= f_off < rraw + rsize:
                return vrva + (f_off - rraw)
        return None

    report.append('--- SECTIONS PE ---')
    for s in sections:
        report.append(f'  {s[0]:<8} RVA: 0x{s[1]:08X} Taille Virt: 0x{s[2]:08X} Raw: 0x{s[3]:08X}')
    report.append('')

    target_strings = [
        b'inline feature 18 evaluation succeeded',
        b'feature 18 created via',
        b'feature 18 evaluate failed',
        b'NR: DLSS feature handle',
        b'not in registry',
        b'real DLSS/DLSSD work left host state incomplete',
        b'skipping inline NR',
        b'discontinuity in',
        b'first NGX evaluate intercepted',
        b'NR upscaling fell back to native',
        b'created inline NR resources',
    ]

    string_locations = {}
    for s in target_strings:
        pos = 0
        locs = []
        while True:
            idx = data.find(s, pos)
            if idx == -1: break
            locs.append(idx)
            pos = idx + len(s)
        string_locations[s.decode('latin1', 'ignore')] = locs
        loc_str = ', '.join([f'0x{x:X} (RVA 0x{file_to_rva(x):X})' for x in locs])
        report.append(f'  \'{s.decode(" latin1\, \ignore\)}\': {loc_str if locs else \NON TROUVE\}')
 report.append('')

 report.append('--- REFERENCES CROISEES (XREFS) ---')
 for s_text, locs in string_locations.items():
 for f_loc in locs:
 s_rva = file_to_rva(f_loc)
 for s_name, vrva, vsize, rraw, rsize in sections:
 if s_name != '.text': continue
 sec_data = data[rraw:rraw+rsize]
 for i in range(len(sec_data) - 4):
 disp = int.from_bytes(sec_data[i:i+4], 'little', signed=True)
 cur_rva = vrva + i + 4
 if cur_rva + disp == s_rva:
 xref_file = rraw + i
 pre = data[max(0, xref_file-3):xref_file]
 report.append(f' [XREF] \'{s_text}\' ref File 0x{xref_file:06X} (RVA 0x{cur_rva-4:06X}) | PreBytes: {pre.hex()}')
 report.append('')

 report.append('--- DESASSEMBLAGE DES SAUTS (0x36E00 - 0x37C00) ---')
 i = 0x36E00
 while i < 0x37C00:
 b1 = data[i]
 b2 = data[i+1] if i+1 < len(data) else 0

 if 0x70 <= b1 <= 0x7F:
 disp8 = int.from_bytes(data[i+1:i+2], 'little', signed=True)
 target = i + 2 + disp8
 opname = {0x74: 'JE/JZ', 0x75: 'JNE/JNZ', 0x72: 'JB', 0x77: 'JA', 0x76: 'JBE', 0x73: 'JAE'}.get(b1, 'Jcc')
 report.append(f' 0x{i:06X}: {opname:<10} -> 0x{target:06X} ({data[i:i+2].hex()})')
 i += 2
 continue

 if b1 == 0x0F and 0x80 <= b2 <= 0x8F:
 disp32 = int.from_bytes(data[i+2:i+6], 'little', signed=True)
 target = i + 6 + disp32
 opname = {0x84: 'JE/JZ', 0x85: 'JNE/JNZ', 0x82: 'JB', 0x87: 'JA', 0x86: 'JBE', 0x83: 'JAE'}.get(b2, 'Jcc_near')
 report.append(f' 0x{i:06X}: {opname:<10} -> 0x{target:06X} ({data[i:i+6].hex()})')
 i += 6
 continue

 if b1 == 0xE9:
 disp32 = int.from_bytes(data[i+1:i+5], 'little', signed=True)
 target = i + 5 + disp32
 report.append(f' 0x{i:06X}: JMP -> 0x{target:06X} ({data[i:i+5].hex()})')
 i += 5
 continue

 i += 1

 report.append('')
 report.append('--- DESASSEMBLAGE ROUTINE INJECTION (0x09F00 - 0x0A500) ---')
 i = 0x09F00
 while i < 0x0A500:
 b1 = data[i]
 b2 = data[i+1] if i+1 < len(data) else 0

 if 0x70 <= b1 <= 0x7F:
 disp8 = int.from_bytes(data[i+1:i+2], 'little', signed=True)
 target = i + 2 + disp8
 opname = {0x74: 'JE/JZ', 0x75: 'JNE/JNZ', 0x72: 'JB', 0x77: 'JA', 0x76: 'JBE', 0x73: 'JAE'}.get(b1, 'Jcc')
 report.append(f' 0x{i:06X}: {opname:<10} -> 0x{target:06X} ({data[i:i+2].hex()})')
 i += 2
 continue

 if b1 == 0x0F and 0x80 <= b2 <= 0x8F:
 disp32 = int.from_bytes(data[i+2:i+6], 'little', signed=True)
 target = i + 6 + disp32
 opname = {0x84: 'JE/JZ', 0x85: 'JNE/JNZ', 0x82: 'JB', 0x87: 'JA', 0x86: 'JBE', 0x83: 'JAE'}.get(b2, 'Jcc_near')
 report.append(f' 0x{i:06X}: {opname:<10} -> 0x{target:06X} ({data[i:i+6].hex()})')
 i += 6
 continue

 i += 1

 with open(out_report_path, 'w', encoding='utf-8') as out_f:
 out_f.write('\n'.join(report))
 print(f'Rapport ecrit : {out_report_path}')

if __name__ == '__main__':
 analyze_renodx(r'D:\Games\AFOP\renodx-dlss5.addon64', r'C:\code\vrdlss5\tools\renodx_eval_map.txt')
