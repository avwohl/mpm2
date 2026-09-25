#!/usr/bin/env python3
"""Check the source build against Digital Research's own binaries.

mpm2src.zip ships two sets of binaries: the V2.0 masters the sources were
cut from (mpm2src/CONTROL) and the V2.1 distribution (mpm2dist).  The
nucleus sources in src/overrides carry both releases behind IFDEF MPM21,
so each one should come back byte for byte.

    python3 tools/verify_dri.py            # both releases
    python3 tools/verify_dri.py 2.1        # just one

For an .SPR only the image and the relocation bits that cover it are
compared.  DRI's linker left stale bytes in the tail of the bitmap, past
the end of the program, which no loader reads and no assembler can
reproduce.

ASM.PRL, RDT.PRL and DDT.COM were made with GENMOD (UTIL1/ASM.SUB,
DDT.SUB), not a linker, and are compared whole, except for the bytes in
UNSET below.  The two masters carry the same three files.  (The copies
next to their sources in mpm2src/UTIL1 are a later rebuild that was never
shipped, and are not a reference; see docs/mpm2_v21.md, "ASM, RDT and
DDT".)

Of MPMLDR.COM only the part MAC assembled is compared (PART below): the
loader's BDOS and the skeleton of its BIOS.  The rest is PL/M, and uplm80
does not compile to PL/M-80's bytes.
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

# (build.py target, file)
TARGETS = [
    ("XDOS", "XDOS.SPR"),
    ("BNKXDOS", "BNKXDOS.SPR"),
    ("RESBDOS", "RESBDOS.SPR"),
    ("TMP", "TMP.SPR"),
    ("ASM", "ASM.PRL"),
    ("RDT", "RDT.PRL"),
    ("DDT", "DDT.COM"),
    ("MPMLDR", "MPMLDR.COM"),
]

# Bytes that no source sets and that the build cannot reproduce, as offsets
# into the file.  In a GENMOD'd program a DS area, or the gap before a
# module's ORG, keeps whatever the program before GENMOD left in memory -
# MAC.COM, which build.py --dri-exact loads there (genmod_memory): that
# accounts for every such byte of RDT.PRL and DDT.COM.  What MAC changed
# while it ran is not modelled: in ASM.PRL that is 11 of MAC's variables,
# as MAC leaves them after assembling AS3SYM.  The same in both releases,
# since both masters carry the same file.  See docs/mpm2_v21.md, "ASM, RDT
# and DDT".
#
# MPMLDR.COM was put together by LOAD (MPMLDR/MPMLDR.SUB), which kept what
# was in memory wherever no HEX record loads a byte: LDRBDOS's DS areas at
# 0E8CH-0EBDH and 0EC0H-0EC3H, and 164DH-16FFH, its variables and the gap
# after them up to LDRBIOS at 1700H.  The build has zeros there.
UNSET = {
    "ASM.PRL": "0BC9 0BD8-0BD9 0C0C-0C0E 0C17-0C18 0C21-0C23",
    "MPMLDR.COM": "0D8C-0DBD 0DC0-0DC3 154D-15FF",
}

# Files of which only part is DRI's assembler source, as file offsets.
# MPMLDR.COM is the PL/M loader (MPMLDR.PLM, LDMONX.ASM) up to 0D00H, then
# LDRBDOS.ASM (0D00H-164CH) and LDRBIOS.ASM (1700H-1742H), which MAC
# assembled, to the end of the file at 177FH.
PART = {
    "MPMLDR.COM": "0C00-167F",
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


def compare_spr(ref, built):
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


def compare_file(ref, built):
    a, b = ref.read_bytes(), built.read_bytes()
    if len(a) != len(b):
        return f"length {len(a)} vs {len(b)}"
    unset = offsets(UNSET.get(ref.name, ""))
    part = sorted(offsets(PART[ref.name])) if ref.name in PART else range(len(a))
    bad = [i for i in part if a[i] != b[i] and i not in unset]
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
    for _, name in TARGETS:
        ref = REFS[version] / name
        built = out / name
        compare = compare_spr if name.endswith(".SPR") else compare_file
        why = compare(ref, built) if built.exists() else "not built"
        if why is None:
            why = "identical to DRI " + version
            if name in PART:
                why += " in " + PART[name]
            unset = offsets(UNSET.get(name, ""))
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
        if v not in REFS:
            ap.error(f"unknown release {v!r}; choose from 2.0, 2.1")
    ok = True
    for v in (args.versions or list(REFS)):
        print(f"MP/M II V{v}:")
        ok = run(v, args.keep) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
