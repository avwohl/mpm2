#!/usr/bin/env python3
"""Check the source build against Digital Research's own binaries.

mpm2src.zip ships two sets of binaries: the V2.0 masters the sources were
cut from (mpm2src/CONTROL, and next to the sources the programs their
submit files made, as in mpm2src/UTIL1) and the V2.1 distribution
(mpm2dist).  The nucleus sources in src/overrides carry both releases
behind IFDEF MPM21, so each one should come back byte for byte.

    python3 tools/verify_dri.py            # both releases
    python3 tools/verify_dri.py 2.1        # just one

For an .SPR only the image and the relocation bits that cover it are
compared.  DRI's linker left stale bytes in the tail of the bitmap, past
the end of the program, which no loader reads and no assembler can
reproduce.

ASM.PRL, RDT.PRL and DDT.COM were made with GENMOD (UTIL1/ASM.SUB,
DDT.SUB), not a linker, and are compared whole, except for the bytes in
UNSET below.
"""
import argparse
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
CONTROL = ROOT / "mpm2_external/mpm2src/CONTROL"
UTIL1 = ROOT / "mpm2_external/mpm2src/UTIL1"
DIST = ROOT / "mpm2_external/mpm2dist"

# (build.py target, file, V2.0 reference, V2.1 reference).  CONTROL's
# ASM.PRL, RDT.PRL and DDT.COM are byte for byte mpm2dist's; UTIL1's are the
# ones the V2.0 source tree's own submit files made.
TARGETS = [
    ("XDOS", "XDOS.SPR", CONTROL, DIST),
    ("BNKXDOS", "BNKXDOS.SPR", CONTROL, DIST),
    ("RESBDOS", "RESBDOS.SPR", CONTROL, DIST),
    ("TMP", "TMP.SPR", CONTROL, DIST),
    ("ASM", "ASM.PRL", UTIL1, DIST),
    ("RDT", "RDT.PRL", UTIL1, DIST),
    ("DDT", "DDT.COM", UTIL1, DIST),
]
VERSIONS = ["2.0", "2.1"]

# Bytes of a GENMOD'd program that no source sets and that the build cannot
# reproduce, as offsets into the file.  A DS area, or the gap before a
# module's ORG, keeps whatever the program before GENMOD left in memory, and
# build.py --dri-exact loads that program's .COM file there (genmod_memory):
# that accounts for every such byte of RDT.PRL and DDT.COM.  What the program
# changed while it ran is not modelled.  In V2.0's ASM.PRL that is 34 bytes
# of PIP's storage above its code, mostly pieces of the HEX text it had been
# copying, such as "61DC54623E5CD48EC" CR LF at 1B8D - and in V2.1's, 11 of
# MAC's variables, as MAC leaves them after assembling AS3SYM.  See
# docs/mpm2_v21.md, "ASM, RDT and DDT".
UNSET = {
    ("2.0", "ASM.PRL"): "185E-185F 186C-1876 196B 196D 1B8D-1B9F",
    ("2.1", "ASM.PRL"): "0BC9 0BD8-0BD9 0C0C-0C0E 0C17-0C18 0C21-0C23",
}


def offsets(ranges):
    out = set()
    for r in ranges.split():
        lo, _, hi = r.partition("-")
        out.update(range(int(lo, 16), int(hi or lo, 16) + 1))
    return out


class Spr:
    def __init__(self, path):
        b = pathlib.Path(path).read_bytes()
        self.length = b[1] | (b[2] << 8)
        self.extra = b[4] | (b[5] << 8)
        self.code = b[256:256 + self.length]
        bits = b[256 + self.length:]
        # the bitmap only describes the program; the rest is padding
        self.bitmap = bits[:(self.length + 7) // 8]


def compare_spr(ref, built, version):
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


def compare_file(ref, built, version):
    a, b = ref.read_bytes(), built.read_bytes()
    if len(a) != len(b):
        return f"length {len(a)} vs {len(b)}"
    unset = offsets(UNSET.get((version, ref.name), ""))
    bad = [i for i in range(len(a)) if a[i] != b[i] and i not in unset]
    if bad:
        return (f"{len(bad)} bytes differ, first at "
                + " ".join(f"{i:04X}" for i in bad[:8]))
    return None


def run(version, keep=None):
    out = pathlib.Path(keep) if keep else pathlib.Path(tempfile.mkdtemp())
    cmd = [sys.executable, str(ROOT / "tools/build.py"),
           "--version", version, "--dri-exact",
           "--output-dir", str(out), *[t[0] for t in TARGETS]]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        return False
    ok = True
    for _, name, ref20, ref21 in TARGETS:
        ref = (ref20 if version == "2.0" else ref21) / name
        built = out / name
        compare = compare_spr if name.endswith(".SPR") else compare_file
        why = compare(ref, built, version) if built.exists() else "not built"
        if why is None:
            why = "identical to DRI " + version
            unset = offsets(UNSET.get((version, name), ""))
            if unset:
                why += f" but for {len(unset)} bytes no source sets"
        print(f"  {name:<12} {why}")
        ok = ok and why.startswith("identical")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("versions", nargs="*", default=[],
                    help="2.0, 2.1 or both (the default)")
    ap.add_argument("--keep", help="build into this directory instead of a temporary one")
    args = ap.parse_args()
    for v in args.versions:
        if v not in VERSIONS:
            ap.error(f"unknown release {v!r}; choose from 2.0, 2.1")
    ok = True
    for v in (args.versions or VERSIONS):
        print(f"MP/M II V{v}:")
        ok = run(v, args.keep) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
