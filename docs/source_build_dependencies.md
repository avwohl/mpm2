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

The equivalent here is `uplm80 --mode mpm`, linked with DRI's own
`PLM_WORK/X0100.ASM`, unmodified, and `src/mpm_pagezero.mac`: the page-zero
addresses are published from modules of their own so that a reference from
compiled code crosses a module boundary and is recorded. uplm80 (0.4.0 on)
passes arguments as PL/M-80 does, the function in C and the parameter in DE,
so X0100's `mon1 equ 0005h+offset` is all MON1 needs to be, as it was for
DRI. The compiler reaches the BDOS entry, the stack top and the warm-boot jump
through names of its own, `??BDOS`, `??MAXB` and `??BOOT`, which
`mpm_pagezero.mac` publishes and no PL/M identifier can capture; `BDOS` itself
is not published, because `UTIL7/DM.PLM` declares a public variable of that
name (DRI's `X0100.ASM` does not publish it either). `.SPR` and `.RSP` images
are still linked at 0 (`ul80 --spr`), because those *are* loaded at the
segment base.

With that fixed, source-built utilities work. On an otherwise all-DRI system,
source-built `USER.PRL` prints `User Number = 0` and `CONSOLE.PRL` prints
`Console = 3`, matching DRI's originals; before the fix both printed nothing.

### Fixed: the CLI indexed the process descriptor table 0x68 bytes too high

A source-built nucleus loaded a `.PRL`, printed its load line, and warm-booted,
whatever the program was. `NUCLEUS/CLI.ASM` reaches the process descriptor
table with

	LXI	H,PDTBL-34H
	LXI	B,0034H

and um80 assembled that as `PDTBL+34H`: the two branches of an external-symbol
expression were swapped, so a constant to the right of a `-` was added instead
of subtracted. That is where the CLI primes a new process's initial stack with
the program's entry address, so the dispatcher resumed each transient at
whatever lay past the table. Fixed in um80; the reference now assembles to the
same bytes as DRI's own `XDOS.SPR`.

Traced by arming an instruction trace at the CLI's `MVI C,90H / JMP XDOS`
(create process) and comparing the two systems at the dispatcher's resume. Both
reach `ld sp,hl` / `ret`; DRI pops `0100` — the transient's entry — and a
source-built system popped `cddb`, which falls through to `jp 0`.

### Fixed: STAT and TOD

`stat` printed its drive line without the free-space figure and repeated it
instead of stopping, and `tod` printed nothing. Both were in what the compiler
and the assembler emitted, and the 0.3.6 CHANGELOG has the defects - among
them, um80 assembled `STAT.PLM`'s `call add(...)` as `CALL 0080H`, 80H being
the opcode of `ADD A,B`. A source-built `stat` now prints
`A: RW, Space:     7,512k` and `tod` prints `Mon 09/14/81 00:00:19`, as DRI's
own binaries do on the same system.

### V2.0 and V2.1

`mpm2_external/mpm2src/NUCLEUS` is MP/M II **V2.0** (`VER.ASM` says so);
everything in `bin/dri` is **V2.1** (1982). V2.1 turned out to be V2.0 plus
in-place patches, and those have been recovered from the binaries into
`src/overrides` behind `IFDEF MPM21` / `$if MPM21`. `build_all.sh --tree=src`
builds V2.0 by default and V2.1 with `--version=2.1`, and in both releases
XDOS, BNKXDOS, RESBDOS and TMP build byte for byte identical to DRI's
(`tools/verify_dri.py`). The one exception is BNKBDOS: the `BNKBDOS.ASM` DRI
shipped with the V2.0 sources is already V2.1's, so a V2.0 build carries the
V2.1 banked BDOS. [mpm2_v21.md](mpm2_v21.md) has every change and how it was
recovered.

## GENSYS is a Python tool, not `GENSYS.COM`

`scripts/gensys.sh` runs `tools/gensys.py` against a JSON configuration; it
does not drive an interactive `GENSYS.COM` under the emulator at all, so the
V2.0/V2.1 prompt differences that used to matter here no longer apply. Both
`bin/dri/GENSYS.COM` and the source-built one are carried on the disk for use
inside MP/M, and neither is part of the host build.

## LDRBDOS (Loader BDOS)

The loader's BDOS is assembled from DRI's `MPMLDR/LDRBDOS.ASM`, and the
skeleton loader BIOS DRI put at 1700H from `MPMLDR/LDRBIOS.ASM`, both with
`um80 --dri --aseg`, as MAC assembled them; they are the same in V2.0 and
V2.1. `tools/build.py` puts `MPMLDR.COM` together the way DRI's `MPMLDR.SUB`
did: the linked PL/M loader at 0100H, LDRBDOS at 0D00H and LDRBIOS at 1700H,
to the end of the last 128-byte record, and writes a byte no statement
loads - the DS areas and the gap before LDRBIOS - as LOAD did, from its
256-byte buffer (`tools/genmod.py`, `load`). 0D00H-177FH is DRI's byte for
byte (`tools/verify_dri.py`). At boot the emulator's own LDRBIOS
(`asm/ldrbios.asm`) is loaded over the skeleton.

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

1. **Source compilation** (`build_src.sh`): Builds 44 targets from source using uplm80/um80/ul80 into `bin/src/`

2. **Disk creation** (`build_hd1k.sh`):
   - Copies source-built binaries from `bin/src/`
   - Copies libraries/docs/samples from `bin/dri/`

3. **Boot sector** (`build_asm.sh`):
   - Assembles LDRBIOS, BNKXIOS, cold boot loader from source
   - Uses MPMLDR (source-built with serial check disabled, but for
     `--dri-exact`)

4. **System generation** (`gensys.sh`):
   - Uses `tools/gensys.py` on the host (see above)
   - Generates MPM.SYS

## What IS Built from Source

With `--tree=src`, these are compiled from `mpm2_external/mpm2src/`:

- All PRL utilities (DIR, STAT, PIP, TYPE, ERA, REN, etc.)
- All SPR system components (BNKBDOS, BNKXDOS, RESBDOS, XDOS, etc.)
- The resident system processes: ABORT.RSP, and MPMSTAT, SCHED and SPOOL each as an `.RSP` and a `.BRS`
- MPMLDR (with serial check disabled via src/overrides/, but for
  `--dri-exact`), with the loader's
  BDOS and skeleton BIOS assembled from DRI's `LDRBDOS.ASM` and `LDRBIOS.ASM`
- Development tools: ASM, RDT and DDT (assembled twice and put together by `tools/genmod.py`, as DRI did with MAC and GENMOD), GENHEX, GENMOD, GENSYS (GENSYS is built for use inside MP/M; the host build uses `tools/gensys.py`)

Source overrides in `src/overrides/` customize:
- MPMLDR - Serial number check disabled (kept, as DRI's, with `--dri-exact`,
  which builds DRI's serial number into the loader and the nucleus)
- NUCLEUS components
- The V2.1 changes, behind `MPM21`, in the nucleus, MPMLDR, GENSYS and the
  UTIL2, UTIL4, UTIL5, UTIL6 and UTIL7 utilities
