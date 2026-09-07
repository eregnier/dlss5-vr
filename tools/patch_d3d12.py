import os
old = " d3d12.dll\.encode(\utf-16le\)
new = \d3dxx.dll\.encode(\utf-16le\)
p = r\D:\Games\AFOP\ReShade64_dlss5.dll\
b = open(p, \rb\).read()
print(\Found:\, b.count(old))
open(p, \wb\).write(b.replace(old, new))
print(\Done! New len:\, os.path.getsize(p))
