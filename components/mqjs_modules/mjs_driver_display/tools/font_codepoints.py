#!/usr/bin/env python3
"""Write the hex codepoint list that gen_font.ps1 consumes.

The atlas is built from a subset face, so pass that TTF as the argument; the
list is written to the project's tools/cjk_codepoints.txt by default.
"""
import argparse
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..'))

parser = argparse.ArgumentParser()
parser.add_argument('ttf', help='font file whose cmap is enumerated')
parser.add_argument('--out', default=os.path.join(PROJECT_DIR, 'tools', 'cjk_codepoints.txt'))
args = parser.parse_args()

d = open(args.ttf, 'rb').read()
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
with open(args.out, "w", encoding="ascii") as f:
    for cp in cps:
        f.write("0x%04X\n" % cp)
print("chars", len(cps), "cjk", sum(1 for c in cps if 0x4E00 <= c <= 0x9FFF))
