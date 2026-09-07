with open(r" D:\Games\AFOP\renodx-dlss5.addon64\, \rb\) as f:
 data = bytearray(f.read())

# 0xe0e0 is 0f 84 bd 00 00 00 (je +0xbd)
# 0xe0db to 0xe0e6: 3d b9 77 18 00 0f 84 bd 00 00 00
# Remplaçons le saut '0f 84 bd 00 00 00' par '90 90 90 90 90 90' (6x NOP)
# pour que le code prenne TOUJOURS la branche de réutilisation du buffer!
p = 0xe0e0
assert data[p:p+6] == b\\x0f\x84\xbd\x00\x00\x00\, \Byte mismatch at 0xe0e0!\
data[p:p+6] = b\\x90\x90\x90\x90\x90\x90\

with open(r\D:\Games\AFOP\renodx-dlss5.addon64\, \wb\) as f:
 f.write(data)
print(\Successfully bypassed workset pool exhaustion check at 0xe0e0!\)