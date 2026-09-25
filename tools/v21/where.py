#!/usr/bin/env python3
"""where.py TARGET [offsets...] - map linked-image offsets back to (module, line, text).

TARGET is XDOS, RESBDOS, BNKXDOS or BNKBDOS.  With no offsets, dumps the whole
map as JSON to <scratch>/<TARGET>.map.json.
"""
import json, os, pathlib, re, subprocess, sys

ROOT = pathlib.Path("/Users/wohl/src/mpm2")
EXT  = ROOT / "mpm2_external/mpm2src"
OVR  = ROOT / "src/overrides"
OUT  = pathlib.Path(os.environ.get("V21", "/tmp/v21")) / "lst2"
OUT.mkdir(parents=True, exist_ok=True)

XDOS_MODULES = ["VER","DATAPG","MPM","RLSMX","RLSDEV","CLI","TICK","CLOCK","ATTACH",
                "XDOS","DSPTCH","QUEUE","FLAG","MEMMGR","TH","LST","PATCH","CLBDOS","XDOSIF"]
TARGETS = {
    "XDOS":    ("NUCLEUS", XDOS_MODULES, False),
    "BNKXDOS": ("NUCLEUS", ["BNKXDOS"], False),
    "RESBDOS": ("NUCLEUS", ["RESBDOS1", "CONBDOS"], True),
    "BNKBDOS": ("BNKBDOS", ["BNKBDOS"], False),
    "TMP":     ("NUCLEUS", ["TMPSUB"], False),
}

def srcfile(sub, m):
    p = OVR / sub / f"{m}.ASM"
    if os.environ.get("PRISTINE"):
        return EXT / sub / f"{m}.ASM"
    return p if p.exists() else EXT / sub / f"{m}.ASM"

ROW = re.compile(r"^\s*(\d+)\s+([0-9A-F]{4})\s+([0-9A-F]{2}(?: [0-9A-F]{2})*)\s*(.*)$")
# um80 puts at most four bytes on the row that carries the source line and
# continues on rows with an address and bytes but no line number.  Without
# these, everything after the first four bytes of a `dw 0,0,0,0,0` is
# unmapped, which is most of a patch area.
CONT = re.compile(r"^\s+([0-9A-F]{4})\s+([0-9A-F]{2}(?: [0-9A-F]{2})*)\s*$")
SEG = re.compile(r"^\s*(?:\w+:)?\s*(cseg|dseg|aseg)\b", re.I)

def listing_rows(prn):
    """-> [(seg, addr, line, nbytes, text)]"""
    seg = "c"
    rows = []
    last = None                      # the row a continuation belongs to
    for l in prn.read_text(encoding="latin-1", errors="replace").splitlines():
        m = ROW.match(l)
        if m:
            text = m.group(4).rstrip()
            s = SEG.match(text.split(";")[0])
            if s:
                seg = s.group(1)[0].lower()
            last = (int(m.group(1)), text)
            rows.append((seg, int(m.group(2), 16), last[0],
                         len(m.group(3).split()), text))
            continue
        c = CONT.match(l)
        if c and last:
            rows.append((seg, int(c.group(1), 16), last[0],
                         len(c.group(2).split()), last[1]))
            continue
        text = l[21:].rstrip()
        s = SEG.match(text.split(";")[0])
        if s:
            seg = s.group(1)[0].lower()
        last = None
    return rows

def build(target):
    sub, mods, concat = TARGETS[target]
    files = []
    if concat:
        cat = OUT / f"{target}.ASM"
        cat.write_text("\n".join(srcfile(sub, m).read_text(encoding="latin-1").rstrip("\x1a")
                                 for m in mods), encoding="latin-1")
        files = [(target, cat)]
    else:
        files = [(m, srcfile(sub, m)) for m in mods]
    inc = ["-I", str(EXT / "UTIL8"), "-I", str(EXT / sub), "-I", str(OVR / sub)]
    rows = {}
    for name, path in files:
        prn = OUT / f"{name}.prn"
        # -t: six-character PUBLIC and EXTRN names, as RMAC wrote them
        r = subprocess.run(["um80", "-t", *inc, "-l", str(prn),
                            "-o", str(OUT / f"{name}.rel"), str(path)],
                           capture_output=True, text=True)
        if not prn.exists():
            sys.exit(f"um80 failed on {path}:\n{r.stdout}\n{r.stderr}")
        rows[name] = listing_rows(prn)
    sys.path.insert(0, "/Users/wohl/src/um80_and_friends")
    from um80.ul80 import Linker
    lk = Linker(); lk.code_base = 0
    for name, _ in files:
        lk.load_rel(str(OUT / f"{name}.rel"))
    lk.link()
    bases = {name: (lk.modules[i].code_base, lk.modules[i].data_base)
             for i, (name, _) in enumerate(files)}
    amap = {}
    for name, _ in files:
        cb, db = bases[name]
        for seg, addr, line, n, text in rows[name]:
            base = db if seg == "d" else cb
            for k in range(n):
                amap.setdefault(base + addr + k, [name, line, text])
    return bases, amap, files

if __name__ == "__main__":
    target = sys.argv[1]
    bases, amap, files = build(target)
    print("bases:", {n: (hex(c), hex(d)) for n, (c, d) in bases.items()}, file=sys.stderr)
    p = OUT.parent / f"{target}.map.json"
    p.write_text(json.dumps({str(k): v for k, v in amap.items()}))
    print(f"{len(amap)} bytes mapped -> {p}", file=sys.stderr)
    for a in sys.argv[2:]:
        o = int(a, 16)
        e = amap.get(o)
        print(f"{o:04X}  " + (f"{e[0]}.ASM:{e[1]}  {e[2]}" if e else "<unmapped>"))
