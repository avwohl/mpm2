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
   - Uses GENSYS.COM from selected tree (`bin/src/` or `bin/dri/`)
   - Extracts LDRBDOS from DRI's MPMLDR.COM
   - Generates MPM.SYS

## What IS Built from Source

With `--tree=src`, these are compiled from `mpm2_external/mpm2src/`:

- All PRL utilities (DIR, STAT, PIP, TYPE, ERA, REN, etc.)
- All SPR system components (BNKBDOS, BNKXDOS, RESBDOS, XDOS, etc.)
- All RSP resident processes (SPOOL, MPMSTAT, ABORT, etc.)
- MPMLDR (with serial check disabled via src/overrides/)
- Development tools: GENSYS, DDT, GENHEX, GENMOD

Source overrides in `src/overrides/` customize:
- MPMLDR - Serial number check disabled
- BNKBDOS - Custom modifications
- NUCLEUS components
