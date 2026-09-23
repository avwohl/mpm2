#!/usr/bin/env python3
"""Check the source build against Digital Research's own binaries.

mpm2src.zip ships two sets of binaries: the V2.0 masters the sources were
cut from (mpm2src/CONTROL) and the V2.1 distribution (mpm2dist).  The
nucleus sources in src/overrides carry both releases behind IFDEF MPM21,
so each one should come back byte for byte.

    python3 tools/verify_dri.py            # both releases
    python3 tools/verify_dri.py 2.1        # just one

Only the .SPR image and the relocation bits that cover it are compared.
DRI's linker left stale bytes in the tail of the bitmap, past the end of
the program, which no loader reads and no assembler can reproduce.
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
REFS = {
    "2.0": ROOT / "mpm2_external/mpm2src/CONTROL",
    "2.1": ROOT / "mpm2_external/mpm2dist",
}
TARGETS = ["XDOS", "BNKXDOS", "RESBDOS", "TMP"]


class Spr:
    def __init__(self, path):
        b = pathlib.Path(path).read_bytes()
        self.length = b[1] | (b[2] << 8)
        self.extra = b[4] | (b[5] << 8)
        self.code = b[256:256 + self.length]
        bits = b[256 + self.length:]
        # the bitmap only describes the program; the rest is padding
        self.bitmap = bits[:(self.length + 7) // 8]


def compare(ref, built):
    a, b = Spr(ref), Spr(built)
    if a.length != b.length:
        return f"program length {a.length:04x} vs {b.length:04x}"
    if a.extra != b.extra:
        return f"extra memory {a.extra:04x} vs {b.extra:04x}"
    bad = [i for i in range(a.length) if a.code[i] != b.code[i]]
    if bad:
        return (f"{len(bad)} code bytes differ, first at "
                + " ".join(f"{i:04X}" for i in bad[:8]))
    bad = [i for i in range(len(a.bitmap)) if a.bitmap[i] != b.bitmap[i]]
    if bad:
        return (f"{len(bad)} relocation bytes differ, first at "
                + " ".join(f"{i:04X}" for i in bad[:8]))
    return None


def run(version, keep=None):
    out = pathlib.Path(keep) if keep else pathlib.Path(tempfile.mkdtemp())
    cmd = [sys.executable, str(ROOT / "tools/build.py"),
           "--version", version, "--dri-exact",
           "--output-dir", str(out), *TARGETS]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        return False
    ok = True
    for t in TARGETS:
        ref = REFS[version] / f"{t}.SPR"
        built = out / f"{t}.SPR"
        why = compare(ref, built) if built.exists() else "not built"
        print(f"  {t:<9} {'identical to DRI ' + version if why is None else why}")
        ok = ok and why is None
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("versions", nargs="*", default=[],
                    help="2.0, 2.1 or both (the default)")
    ap.add_argument("--keep", help="build into this directory instead of a temporary one")
    args = ap.parse_args()
    for v in args.versions:
        if v not in REFS:
            ap.error(f"unknown release {v!r}; choose from 2.0, 2.1")
    ok = True
    for v in (args.versions or ["2.0", "2.1"]):
        print(f"MP/M II V{v}:")
        ok = run(v, args.keep) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
