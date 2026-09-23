#!/usr/bin/env python3
"""syms.py TARGET [addr...] - full linked symbol table (via um80 -g + ul80 -s)."""
import os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where

target = sys.argv[1]
sub, mods, concat = where.TARGETS[target]
bases, amap, files = where.build(target)          # also writes the concat file
inc = ["-I", str(where.EXT / "UTIL8"), "-I", str(where.EXT / sub), "-I", str(where.OVR / sub)]
rels = []
for name, path in files:
    r = where.OUT / f"{name}.g.rel"
    subprocess.run(["um80", "-g", *inc, "-o", str(r), str(path)], capture_output=True)
    rels.append(str(r))
symf = where.OUT / f"{target}.sym"
subprocess.run(["ul80", "--spr", "-S", str(symf), "-o", str(where.OUT / f"{target}.spr"),
                *rels], capture_output=True)
tab = sorted((int(a, 16), n) for a, n in
             re.findall(r"([0-9A-Fa-f]{4})\s+(\S+)", symf.read_text()))
if len(sys.argv) > 2:
    for a in sys.argv[2:]:
        x = int(a, 16)
        le = [t for t in tab if t[0] <= x]
        ge = [t for t in tab if t[0] > x]
        names = [n for v, n in le if v == le[-1][0]] if le else []
        e = amap.get(x)
        print(f"{x:04X}: {'/'.join(names)}+{x-le[-1][0] if le else 0}"
              + (f"   (next {ge[0][1]}@{ge[0][0]:04X})" if ge else "")
              + (f"   {e[0]}.ASM:{e[1]}  {e[2]}" if e else ""))
else:
    for v, k in tab:
        print(f"{v:04X} {k}")
