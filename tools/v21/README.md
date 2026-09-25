# V2.1 reconstruction tools

The research tools the MP/M II V2.1 reconstruction was done with, kept for
whoever takes it further.  `docs/mpm2_v21.md` describes every change they
found and how each was checked.

`tools/verify_dri.py`, one directory up, is the one that is part of the build:
it checks a source build against Digital Research's own binaries.  Nothing here
is.

| Script | Use |
|--------|-----|
| `spr.py A B` | Diff two `.SPR`/`.PRL` images: header, differing runs, which bytes are flagged for relocation, which bitmap bits differ. |
| `where.py T [off...]` | Map an offset in a linked image back to (module, source line, text) for target `T` - `XDOS`, `BNKXDOS`, `RESBDOS`, `BNKBDOS` or `TMP`. |
| `syms.py T [addr...]` | The linked symbol table: every label of every module (`um80 -g`), at the module bases of `where.py`'s link.  Answers "what is at 2081H". |
| `annot.py T A B` | `spr.py` and `where.py` together: a diff of two images with every run annotated with the source lines it covers.  This is the one to reach for. |
| `disasm.py f s e` | Disassemble one address range of a raw image at its real address.  `ud80` stops at the first `RET` and dumps the rest as `DB`, so each routine has to be disassembled from its own entry point. |
| `rawdiff.py A B [gap]` | Plain byte diff with ASCII, for files with no SPR header. |

```bash
python3 tools/v21/annot.py XDOS mpm2_external/mpm2src/CONTROL/XDOS.SPR \
                                mpm2_external/mpm2dist/XDOS.SPR
python3 tools/v21/rawdiff.py mpm2_external/mpm2src/CONTROL/PIP.PRL \
                             mpm2_external/mpm2dist/PIP.PRL
python3 tools/v21/disasm.py mpm2_external/mpm2dist/PIP.PRL 1fe1 2040
```

Two knobs:

* `V21=<dir>` is where listings and `.rel` files go (default `/tmp/v21`).
* `PRISTINE=1` makes `where.py` ignore `src/overrides` and use the untouched
  `mpm2_external` sources.  Wanted for `TMP`, whose override carries 25 bytes
  of local fix that shift every offset after `00CD`.  It does *not* work for
  `XDOS`, `RESBDOS` or `BNKBDOS`.  DRI's text spells some names two ways,
  which RMAC takes for one name because it ignores a `$` in a name, and um80
  does not: `MPM.ASM` stores to `nmb$lst`, which `DATAPG.ASM` defines as
  `nmblst`; `CLI.ASM` calls `open$test` and defines `opentest`;
  `RESBDOS1.ASM` calls `SET$DMA$BUFA` as well as `SET$DMABUFA`, and
  loads `common$fcb` as well as `commonfcb`; and
  `BNKBDOS.ASM` has 23 such names.  And `MEMMGR.ASM` ends six lines with 8AH,
  a line feed with the parity bit set, which um80 does not take for one.
  For these leave it unset - with no `-D MPM21` the XDOS and RESBDOS
  overrides assemble to the V2.0 layout, which matches DRI's V2.0 images
  exactly, and BNKBDOS's to DRI's V2.1 `BNKBDOS.SPR`, the only one its
  source builds.

`ds` reserves space without emitting listing bytes, so a run that falls inside
one cannot be mapped.  The only place that bites is `pdtbl` entry 0's `ds 36`
in `DATAPG.ASM`; the `db 0ffh` in front of it identifies the spot.

The paths to the repository and to `um80_and_friends` are hardcoded, and
`where.py` imports `um80.ul80.Linker` to get each module's segment bases.
