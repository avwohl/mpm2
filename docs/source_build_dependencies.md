# Source Build Dependencies

When using `./scripts/build_all.sh --tree=src`, most MP/M II utilities are compiled from source. However, some files still come from DRI binaries because no source code is available.

## Development Tools (No Source Available)

These tools are shipped in `bin/dri/` only. They have no corresponding source code in the MP/M II distribution:

| File | Size | Purpose |
|------|------|---------|
| LINK.COM | 15,616 | LINK-80 linker (superseded by ul80) |
| RMAC.COM | 13,568 | MACRO-80 assembler (superseded by um80) |
| LIB.COM | 7,168 | Library manager for .REL/.LIB files |
| LOAD.COM | 5,120 | HEX to COM loader |
| XREF.COM | 15,488 | Cross-reference generator |

**Note:** LINK.COM and RMAC.COM are not needed for the source build - the build system uses native `ul80` and `um80` tools instead.

## Why `--tree=src` did not produce a working system

There were two separate faults here, and an earlier version of this document
attributed both to the same cause. That was wrong, and the record is corrected
below.

### Fixed: transient `.PRL` files were linked one page low

Every source-built utility produced no output, whichever nucleus it ran on,
because `ul80 --prl` linked a transient at ORG 0.

MP/M's relocator adds the memory segment's base *page* to each byte the
relocation bitmap marks (`NUCLEUS/CLI.ASM`, `relocate`), but the CLI loads a
transient at `segment$bottom + 0100H`. The extra page has to come from the
link, so a `.PRL` image must be linked at 0100H — the same origin a `.COM`
uses. Linked at 0, every relocated address landed one page below the code.
DRI's own binaries show the convention: the highest relocatable word in
`STAT.PRL` is `program_length + 0100H`.

Page zero was the other half. Under MP/M it belongs to the process's memory
segment, so the BDOS entry (0005H), the default FCB (005CH) and the DMA buffer
(0080H) have to be relocated as well — DRI's `DIR.PRL` marks twelve `CALL 5`
sites in its bitmap. Only a resolved *symbol* reference reaches the bitmap, so
the compiler must not emit those addresses as literals. DRI solved this by
linking each utility twice against `PLM_WORK/X0100.ASM` and `X0200.ASM`, which
differ only in an `offset` equate, and diffing the two images with GENMOD.

The equivalent here is `uplm80 --mode mpm` plus `src/mpm_pagezero.mac`: the
page-zero addresses are published from a module of their own so that a
reference from the runtime or from compiled code crosses a module boundary and
is recorded. `.SPR` and `.RSP` images are still linked at 0 (`ul80 --spr`),
because those *are* loaded at the segment base.

With that fixed, source-built utilities work. On an otherwise all-DRI system,
source-built `USER.PRL` prints `User Number = 0` and `CONSOLE.PRL` prints
`Console = 3`, matching DRI's originals; before the fix both printed nothing.

### Open: the source-built V2.0 nucleus

A source-built nucleus still fails, and this part is a real V2.0/V2.1
difference. `mpm2_external/mpm2src/NUCLEUS` is MP/M II **V2.0** (`VER.ASM` says
so, and a source-built system banners as "MP/M II V2.0", 1981); everything in
`bin/dri` is **V2.1** (1982).

Substituting whole modules into an otherwise source-built system, and probing
with a hand-written `.PRL` that prints the word at page-zero offset 6:

| nucleus modules taken from `bin/dri` | probe |
|---|---|
| `XDOS` + `BNKXDOS` + `RESBDOS` + `BNKBDOS` | prints `BFFD` |
| `XDOS` + `BNKXDOS` | prints `BFFD` |
| `RESBDOS` + `BNKBDOS` | no output |
| `XDOS` alone | no output |
| `BNKXDOS` alone | no output |

So the fault is in the source-built `XDOS`/`BNKXDOS` pair, and the two have to
match. `BNKBDOS.SPR` and `TMP.SPR` build **byte-identical** to DRI's, so the
assembler and linker are reproducing DRI's own output exactly for those.

What a transient can and cannot do on a source-built nucleus, each tested with
a minimal assembly `.PRL`:

| program | result |
|---|---|
| set SP, spin, `jp` page-zero 0 (no XDOS call) | returns to the prompt |
| XDOS function 12 (return version), then exit | returns to the prompt |
| XDOS function 2 (console output) | kills the session |
| XDOS function 0 (system reset) | kills the session |

So the `CALL 5` chain the CLI sets up — `segment$bottom+5` jumps to `top-3`,
which jumps to `xbdos` — is intact and reaches XDOS, and process termination
through page-zero 0 works. Only certain XDOS functions are fatal.

V2.1 appears to be V2.0 plus in-place patches rather than a recompile: every
nucleus module has exactly the same program length in both trees, `PATCH.ASM`'s
128 reserved zero bytes are filled with code in DRI's `XDOS.SPR`, and V2.0 call
sites are rewritten to call into that area (at 0x01F8 the V2.0 `lxi h,0016 /
dad d / mov m,b` becomes `call 1814H`, and at 0x0527 `lhld 2081H` becomes
`call 183FH`). Reconstructing those patches from the V2.0 sources is what is
left to do. `BNKXDOS` is one visible instance: V2.1 holds a six-byte routine
(`dcx b / ldax b / ani 0Fh / mov c,a / ret`) at program offset 0x08, where the
V2.0 source has `dw 0,0,0` in `ProcAddressTable` slots 2-4.

Until that is done, use `--tree=dri` for a runnable system. `--tree=src`
builds every module and produces working utilities, but its nucleus does not
run transients.

## GENSYS is a Python tool, not `GENSYS.COM`

`scripts/gensys.sh` runs `tools/gensys.py` against a JSON configuration; it
does not drive an interactive `GENSYS.COM` under the emulator at all, so the
V2.0/V2.1 prompt differences that used to matter here no longer apply. Both
`bin/dri/GENSYS.COM` and the source-built one are carried on the disk for use
inside MP/M, and neither is part of the host build.

## LDRBDOS (Loader BDOS)

The loader's BDOS component (LDRBDOS) has no separate source. It is extracted from DRI's pre-built MPMLDR.COM at file offset 0xC00 (2,688 bytes).

This extraction happens in `tools/build.py` during the MPMLDR build:
```python
# LDRBDOS at memory 0xD00, file offset 0xC00
ldrbdos_data = dri_mpmldr[0xC00:0xC00 + 0xA80]
```

## Macro Libraries and Documentation

These reference files are always copied from `bin/dri/` regardless of build tree:

### Libraries (.LIB)
- DISKDEF.LIB - Disk parameter macros
- Z80.LIB - Z80 instruction set macros
- SEQIO.LIB - Sequential I/O macros
- DSTACK.LIB - Stack manipulation macros
- Plus: SELECT, STACK, COMPARE, NCOMPARE, BUTTONS, DOWHILE, INTER, SIMPIO, TREADLES, WHEN, I8085

### Documentation (.DOC)
- Z80.DOC - Z80 instruction reference
- DISK.DOC - Disk format documentation

### Sample Source (.ASM)
- BOOT.ASM, DEBLOCK.ASM, DUMP.ASM
- LDRBIOS.ASM, RESXIOS.ASM, TODCNV.ASM

## Build Process Summary

1. **Source compilation** (`build_src.sh`): Builds ~40 utilities from source using uplm80/um80/ul80

2. **Disk creation** (`build_hd1k.sh`):
   - Copies source-built binaries from `bin/src/`
   - Copies libraries/docs/samples from `bin/dri/`

3. **Boot sector** (`build_asm.sh`):
   - Assembles LDRBIOS, BNKXIOS, cold boot loader from source
   - Uses MPMLDR (source-built with serial check disabled)

4. **System generation** (`gensys.sh`):
   - Uses `tools/gensys.py` on the host (see above)
   - Extracts LDRBDOS from DRI's MPMLDR.COM
   - Generates MPM.SYS

## What IS Built from Source

With `--tree=src`, these are compiled from `mpm2_external/mpm2src/`:

- All PRL utilities (DIR, STAT, PIP, TYPE, ERA, REN, etc.)
- All SPR system components (BNKBDOS, BNKXDOS, RESBDOS, XDOS, etc.)
- All RSP resident processes (SPOOL, MPMSTAT, ABORT, etc.)
- MPMLDR (with serial check disabled via src/overrides/)
- Development tools: DDT, GENHEX, GENMOD, GENSYS (GENSYS is built for use inside MP/M; the host build uses `tools/gensys.py`)

Source overrides in `src/overrides/` customize:
- MPMLDR - Serial number check disabled
- BNKBDOS - Custom modifications
- NUCLEUS components
