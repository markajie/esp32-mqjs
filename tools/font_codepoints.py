import struct
src = r"D:\esp\projects\my-esp32-mqjs\main\fonts\NotoSansSC-Regular-sub.ttf"
out = r"D:\esp\projects\my-esp32-mqjs\tools\cjk_codepoints.txt"
d = open(src, "rb").read()
num = struct.unpack(">H", d[4:6])[0]
tables = {}
for i in range(num):
    o = 12 + 16 * i
    tag = d[o:o+4].decode("latin1")
    tables[tag] = struct.unpack(">II", d[o+8:o+16])
co, cl = tables["cmap"]
n = struct.unpack(">H", d[co+2:co+4])[0]
cps = set()
for i in range(n):
    plat, enc, off = struct.unpack(">HHI", d[co+4+8*i:co+12+8*i])
    o = co + off
    fmt = struct.unpack(">H", d[o:o+2])[0]
    if fmt != 4:
        continue
    seg = struct.unpack(">H", d[o+6:o+8])[0] // 2
    end = struct.unpack(">" + "H" * seg, d[o+14:o+14+2*seg])
    start = struct.unpack(">" + "H" * seg, d[o+16+2*seg:o+16+4*seg])
    for s, e in zip(start, end):
        if e != 0xFFFF:
            cps.update(range(s, e + 1))
cps = sorted(cps)
with open(out, "w", encoding="ascii") as f:
    for cp in cps:
        f.write("0x%04X\n" % cp)
print("chars", len(cps), "cjk", sum(1 for c in cps if 0x4E00 <= c <= 0x9FFF))
