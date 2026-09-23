#!/usr/bin/env python3
"""dis.py <binfile> <start_hex> <end_hex> — disassemble one range at its real address."""
import pathlib, subprocess, sys, tempfile, os, re
b = pathlib.Path(sys.argv[1]).read_bytes()
s = int(sys.argv[2], 16); e = int(sys.argv[3], 16)
with tempfile.TemporaryDirectory() as d:
    frag = os.path.join(d, "f.bin"); out = os.path.join(d, "f.asm")
    pathlib.Path(frag).write_bytes(b[s:e+1])
    subprocess.run(["ud80", "--org", hex(s), "-o", out, frag],
                   capture_output=True)
    for line in pathlib.Path(out).read_text().splitlines():
        t = line.rstrip()
        if not t or t.lstrip().startswith(";") or re.search(r"\.8080|\bORG\b|\bEND\b", t):
            continue
        print(t)
