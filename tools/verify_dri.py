#!/usr/bin/env python3
"""Check the source build against Digital Research's own binaries.

mpm2src.zip ships two sets of binaries: the V2.0 masters the sources were
cut from (mpm2src/CONTROL) and the V2.1 distribution (mpm2dist).  The
nucleus sources in src/overrides carry both releases behind IFDEF MPM21,
so each one should come back byte for byte.

    python3 tools/verify_dri.py            # both releases
    python3 tools/verify_dri.py 2.1        # just one

Every file is compared whole, header page, image, relocation bit map and
the padding of the last record, but for the bytes in UNSET below and
outside the PART of a file that is DRI's assembler source.  A difference
is reported by the part it is in, at its offset in that part: for an
.SPR, .RSP or .PRL the header page, the image, the bit map or the padding
after it.

The nucleus, BNKBDOS, ABORT.RSP and DUMP.PRL are RMAC modules DRI linked
with LINK, which fills the last record with ^Z, as tools/build.py does
(Builder.pad_as_link).  The BNKBDOS.ASM DRI shipped with the V2.0 sources
is V2.1's, so BNKBDOS.SPR is compared with V2.1's only.

ASM.PRL, RDT.PRL and DDT.COM were made with GENMOD (UTIL1/ASM.SUB,
DDT.SUB), not a linker.  The two masters carry the same three files.
(The copies next to their sources in mpm2src/UTIL1 are a later rebuild
that was never shipped, and are not a reference; see docs/mpm2_v21.md,
"ASM, RDT and DDT".)

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

# (build.py target, file, the releases it is compared in)
BOTH = ("2.0", "2.1")
TARGETS = [
    ("XDOS", "XDOS.SPR", BOTH),
    ("BNKXDOS", "BNKXDOS.SPR", BOTH),
    ("RESBDOS", "RESBDOS.SPR", BOTH),
    ("TMP", "TMP.SPR", BOTH),
    ("BNKBDOS", "BNKBDOS.SPR", ("2.1",)),
    ("ABORT", "ABORT.RSP", BOTH),
    ("DUMP", "DUMP.PRL", BOTH),
    ("ASM", "ASM.PRL", BOTH),
    ("RDT", "RDT.PRL", BOTH),
    ("DDT", "DDT.COM", BOTH),
    ("MPMLDR", "MPMLDR.COM", BOTH),
]

# Why a file is not compared in a release it is left out of.
NOT_COMPARED = {
    "BNKBDOS.SPR": "not compared: the BNKBDOS.ASM DRI shipped is V2.1's",
}

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


def parts(name, data):
    """[(first offset, end offset, what)] - the parts of a file."""
    if not name.endswith((".SPR", ".RSP", ".PRL", ".BRS")):
        return [(0, len(data), "file")]
    length = data[1] | (data[2] << 8)
    image = 0x100 + length
    bitmap = image + (length + 7) // 8
    return [(0, 0x100, "header"), (0x100, image, "image"),
            (image, bitmap, "bit map"), (bitmap, len(data), "padding")]


def compare(ref, built):
    """None if `built' is DRI's `ref', else what differs."""
    a, b = ref.read_bytes(), built.read_bytes()
    name = ref.name
    if name.endswith((".SPR", ".RSP", ".PRL", ".BRS")):
        for lo, what in ((1, "program length"), (4, "extra memory")):
            x, y = a[lo] | (a[lo + 1] << 8), b[lo] | (b[lo + 1] << 8)
            if x != y:
                return f"{what} {x:04x} vs {y:04x}"
    if len(a) != len(b):
        return f"length {len(a)} vs {len(b)}"
    unset = offsets(UNSET.get(name, ""))
    part = offsets(PART[name]) if name in PART else range(len(a))
    bad = [i for i in part if a[i] != b[i] and i not in unset]
    if not bad:
        return None
    out = []
    for lo, hi, what in parts(name, a):
        here = [i for i in bad if lo <= i < hi]
        if here:
            out.append(f"{len(here)} {what} bytes differ, first at "
                       + " ".join(f"{i - lo:04X}" for i in here[:8]))
    return "; ".join(out)


def run(version, keep=None):
    out = pathlib.Path(keep) if keep else pathlib.Path(tempfile.mkdtemp())
    targets = [t for t in TARGETS if version in t[2]]
    cmd = [sys.executable, str(ROOT / "tools/build.py"),
           "--version", version, "--dri-exact",
           "--output-dir", str(out), *sorted({t[0] for t in targets})]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr)
        return False
    ok = True
    for _, name, releases in TARGETS:
        if version not in releases:
            print(f"  {name:<12} {NOT_COMPARED[name]}")
            continue
        ref = REFS[version] / name
        built = out / name
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
