#!/usr/bin/env python3
"""annot.py TARGET A.SPR B.SPR - diff two SPRs and annotate each run with source lines."""
import json, os, sys, pathlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from spr import Spr, runs
import where

target = sys.argv[1]
A, B = Spr(sys.argv[2]), Spr(sys.argv[3])
bases, amap, files = where.build(target)
print("bases:", {n: (hex(c), hex(d)) for n, (c, d) in bases.items()})
for s, e in runs(A.code, B.code, gap=8):
    print(f"\n=== {s:04X}-{e:04X} ===")
    seen = []
    for i in range(s, e + 1):
        ent = amap.get(i)
        k = (ent[0], ent[1]) if ent else None
        if k not in [x[0] for x in seen]:
            seen.append((k, i))
    for k, i in seen:
        ent = amap.get(i)
        print(f"  {i:04X} " + (f"{ent[0]}.ASM:{ent[1]}  {ent[2]}" if ent else "<unmapped>"))
    print("   a: " + " ".join(f"{x:02x}" for x in A.code[s:e+1]))
    print("   b: " + " ".join(f"{x:02x}" for x in B.code[s:e+1]))
    print("   R: " + "".join("A" if A.reloc(i) and B.reloc(i) else
                             ("a" if A.reloc(i) else ("b" if B.reloc(i) else ".")) for i in range(s, e+1)))
