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

## The source tree is V2.0 and does not interoperate with V2.1

This is the single reason `--tree=src` does not give a working system, and it
is worth stating precisely because it looks like a bug in the build and is not.

`mpm2_external/mpm2src/NUCLEUS` is MP/M II **V2.0** (`VER.ASM` says so, and a
source-built system banners as "MP/M II V2.0", 1981). Everything in `bin/dri`
is **V2.1** (1982). A system built from source is therefore a genuine V2.0
MP/M II, and the V2.1 utilities on the disk do not work on it.

Measured, same disk and same emulator, only MPM.SYS regenerated:

| system | `stat` |
|---|---|
| all `bin/dri` (V2.1) | works, 6 of 6 sequential sessions |
| all `bin/src` (V2.0) | no output, 6 of 6 sessions |

The difference is not in one module. Substituting single modules into an
otherwise all-DRI system, counting sessions that complete a `dir`:

| system | sessions passing |
|---|---|
| all dri (control) | 30/30 |
| dri + src `BNKBDOS.SPR` | 8/8 |
| dri + src `TMP.SPR` | 12/12 (byte-identical in both trees) |
| dri + src `XDOS.SPR` | 2/10 |
| dri + src `RESBDOS.SPR` | 0/8 |
| dri + src `BNKXDOS.SPR` | 1/8 |

So installing DRI's V2.1 `XDOS.SPR` alone does not fix it, and `RESBDOS` or
`BNKXDOS` alone each reproduce it. One concrete instance of the delta: V2.1
hides a six-byte routine (`dcx b / ldax b / ani 0Fh / mov c,a / ret`) in
`BNKXDOS` ProcAddressTable slots 2-4 at program offset 0x08, which the V2.0
sources do not have.

Fixing this needs V2.1 nucleus sources, which are not in this repository.
Until then `--tree=src` is useful for checking that the toolchain builds
everything, not for producing a runnable system; use `--tree=dri` for that.

## GENSYS.COM Version Mismatch

The source code in `mpm2_external/mpm2src/` is **MP/M II V2.0**, but the DRI binaries are **V2.1**.

GENSYS.COM must use the DRI V2.1 binary because:
- V2.0 and V2.1 have different interactive prompts
- V2.1 adds "Enable Compatibility Attributes (N)?" prompt
- The `gensys.sh` expect script is written for V2.1 prompts
- Using V2.0 GENSYS.COM causes prompt timeout failures

The source-built GENSYS.COM (8,832 bytes, V2.0) exists in `bin/src/` but is not used.
The DRI GENSYS.COM (9,472 bytes, V2.1) is always used by `gensys.sh`.

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
   - Uses GENSYS.COM from DRI (V2.1 required, see above)
   - Extracts LDRBDOS from DRI's MPMLDR.COM
   - Generates MPM.SYS

## What IS Built from Source

With `--tree=src`, these are compiled from `mpm2_external/mpm2src/`:

- All PRL utilities (DIR, STAT, PIP, TYPE, ERA, REN, etc.)
- All SPR system components (BNKBDOS, BNKXDOS, RESBDOS, XDOS, etc.)
- All RSP resident processes (SPOOL, MPMSTAT, ABORT, etc.)
- MPMLDR (with serial check disabled via src/overrides/)
- Development tools: DDT, GENHEX, GENMOD (GENSYS built but not used - V2.0/V2.1 mismatch)

Source overrides in `src/overrides/` customize:
- MPMLDR - Serial number check disabled
- BNKBDOS - Custom modifications
- NUCLEUS components
