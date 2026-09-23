#!/usr/bin/env python3
"""spr.py — SPR/PRL helpers: header, code image, relocation bitmap."""
import pathlib, sys

class Spr:
    def __init__(self, path):
        self.path = pathlib.Path(path)
        b = self.path.read_bytes()
        self.raw = b
        self.length = b[1] | (b[2] << 8)
        self.extra  = b[4] | (b[5] << 8)
        self.code = b[256:256 + self.length]
        self.bitmap = b[256 + self.length:]
    def reloc(self, off):
        """True if the byte at code offset `off` is flagged in the bitmap."""
        i, bit = divmod(off, 8)
        if i >= len(self.bitmap):
            return False
        return bool(self.bitmap[i] & (0x80 >> bit))
    def __repr__(self):
        return (f"<{self.path.name} len={self.length:04x} extra={self.extra:04x} "
                f"bitmap={len(self.bitmap)}>")

def runs(a, b, gap=4):
    """Yield (start, end) inclusive ranges where a and b differ, merging gaps."""
    n = max(len(a), len(b))
    d = [i for i in range(n) if (a[i] if i < len(a) else None) != (b[i] if i < len(b) else None)]
    out = []
    for i in d:
        if out and i - out[-1][1] <= gap:
            out[-1][1] = i
        else:
            out.append([i, i])
    return [tuple(r) for r in out]

if __name__ == "__main__":
    A, B = Spr(sys.argv[1]), Spr(sys.argv[2])
    print(A); print(B)
    for s, e in runs(A.code, B.code):
        ha = " ".join(f"{x:02x}" for x in A.code[s:e+1])
        hb = " ".join(f"{x:02x}" for x in B.code[s:e+1])
        rel = "".join("R" if A.reloc(i) else ("r" if B.reloc(i) else ".") for i in range(s, e+1))
        print(f"{s:04X}-{e:04X} {rel}\n   a: {ha}\n   b: {hb}")
    ba = A.bitmap; bb = B.bitmap
    if ba != bb:
        diffs = [i*8+j for i in range(max(len(ba),len(bb)))
                 for j in range(8)
                 if ((ba[i] if i<len(ba) else 0) ^ (bb[i] if i<len(bb) else 0)) & (0x80>>j)]
        print(f"bitmap differs at code offsets: {' '.join(f'{x:04X}' for x in diffs)}")
