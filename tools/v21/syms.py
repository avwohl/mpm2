#!/usr/bin/env python3
"""syms.py TARGET [addr...] - full linked symbol table (via um80 -g and where.py's link)."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import where

target = sys.argv[1]
sub, mods, concat = where.TARGETS[target]
bases, amap, files = where.build(target)          # also writes the concat file
from um80.ul80 import Linker                       # the installed um80, as where.py's
from um80.relformat import ADDR_PROGRAM_REL, ADDR_DATA_REL
inc = ["-I", str(where.EXT / "UTIL8"), "-I", str(where.EXT / sub), "-I", str(where.OVR / sub)]
# um80 -g makes every label PUBLIC.  With -t as well it would cut them all to
# six characters, and without -t the nucleus does not link (DSPTCH's
# `memsegtbl' is DATAPG's `memseg').  So each module is assembled with -g
# and without -t, and its symbols placed at the module bases of
# where.build()'s link, which has RMAC's six-character PUBLIC and EXTRN
# names.  --dri reads the source as RMAC does, so a name comes out without
# its `$' signs, as in RMAC's own symbol table.
tab = []
for name, path in files:
    r = where.OUT / f"{name}.g.rel"
    subprocess.run(["um80", "--dri", "-g", *inc, "-o", str(r), str(path)], capture_output=True)
    lk = Linker()
    lk.load_rel(str(r))
    cb, db = bases[name]
    for sym, (value, seg) in lk.modules[0].publics.items():
        tab.append(({ADDR_PROGRAM_REL: cb, ADDR_DATA_REL: db}.get(seg, 0) + value, sym))
tab.sort()
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
