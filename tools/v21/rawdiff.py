#!/usr/bin/env python3
"""rawdiff.py A B [gap] - byte diff of two raw files, with ASCII."""
import sys, pathlib
a = pathlib.Path(sys.argv[1]).read_bytes(); b = pathlib.Path(sys.argv[2]).read_bytes()
gap = int(sys.argv[3]) if len(sys.argv) > 3 else 8
n = max(len(a), len(b)); out = []
for i in range(n):
    x = a[i] if i < len(a) else None; y = b[i] if i < len(b) else None
    if x != y:
        if out and i - out[-1][1] <= gap: out[-1][1] = i
        else: out.append([i, i])
for s, e in out:
    pa = " ".join(f"{c:02x}" for c in a[s:e+1]); pb = " ".join(f"{c:02x}" for c in b[s:e+1])
    ta = "".join(chr(c) if 32 <= c < 127 else "." for c in a[s:e+1])
    tb = "".join(chr(c) if 32 <= c < 127 else "." for c in b[s:e+1])
    print(f"{s:04X}-{e:04X}\n  2.0: {pa}  |{ta}|\n  2.1: {pb}  |{tb}|")
