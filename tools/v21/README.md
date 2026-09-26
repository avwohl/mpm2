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

* `V21=<dir>` is where listings and `.rel` files go (default `build/v21` in
  the checkout the scripts are in).
* `PRISTINE=1` makes `where.py` ignore `src/overrides` and use the untouched
  `mpm2_external` sources.  Wanted for `TMP`, whose override carries 25 bytes
  of local fix that shift every offset after `00CD`.  For the others it makes
  no difference to an offset: with no `-D MPM21` the XDOS and RESBDOS
  overrides assemble to the V2.0 layout, which is DRI's V2.0 image, and
  BNKBDOS has no override (its source builds DRI's V2.1 `BNKBDOS.SPR`, the
  only one it builds).
  `where.py` and `syms.py` assemble with `um80 --dri`, which reads DRI's
  text as RMAC does: a `$` in a name is ignored, so `MPM.ASM`'s `nmb$lst`
  is `DATAPG.ASM`'s `nmblst`, and `syms.py` prints names without their `$`
  signs, as RMAC's symbol table has them.

`ds` reserves space without emitting listing bytes, so a run that falls inside
one cannot be mapped.  The only place that bites is `pdtbl` entry 0's `ds 36`
in `DATAPG.ASM`; the `db 0ffh` in front of it identifies the spot.

The sources are the ones in the checkout the scripts are in, and `um80` is
the installed one: `where.py` and `syms.py` run the `um80` on the `PATH` and
import `um80.ul80.Linker`, to get each module's segment bases, from the
installed package.
