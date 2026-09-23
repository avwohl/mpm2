# MP/M II V2.1

`mpm2src.zip` is the V2.0 source release.  Digital Research never put the
V2.1 sources on a distribution disk, and no archive has them: bitsavers,
the Tim Olmstead collection, cpm.z80.de and the Unofficial CP/M Web Site
all carry the same two files, `mpm2src.zip` (sources, V2.0) and
`mpm_ii.zip` (binaries, V2.1).  What follows is a reconstruction of V2.1
from the binaries, checked against them byte for byte.

## Where the reference binaries are

The archive ships both releases, which is what makes the reconstruction
checkable:

| Tree                             | Release | Serial              |
|----------------------------------|---------|---------------------|
| `mpm2_external/mpm2src/CONTROL`  | V2.0    | `00 14 01 00 00 01` |
| `mpm2_external/mpm2src/NUCLEUS`  | V2.0    | unserialized, `654321` |
| `mpm2_external/mpm2src/UTIL*`    | V2.0    | unserialized        |
| `mpm2_external/mpm2dist`         | V2.1    | `00 14 01 00 09 2f` |
| `mpm2_external/disk1_files`      | V2.1    | `00 14 01 00 09 2f` |
| `bin/dri`                        | V2.1    | `00 14 01 00 09 2f` |

The serial number is stamped on the distribution master by `MPM2SRL.COM`
and is not a version difference: `LIB.COM`, `RMAC.COM` and `MPMLDR.COM`
differ between the two masters only in the last two bytes of it.

## Building either release

```
python3 tools/build.py --version 2.1          # or 2.0, the default
./scripts/build_all.sh --tree=src --version=2.1
```

`--version 2.1` defines `MPM21` for `um80` and for `uplm80`, which is
what the conditional blocks in `src/overrides` are keyed on.  Two more
switches exist for checking the result against Digital Research's own
binaries:

* `--serial dri` builds in the serial number from DRI's master instead
  of the `654321` placeholder the nucleus sources carry.
* `--dri-exact` implies `--serial dri` and additionally leaves out the
  local fixes this repository carries on top of DRI's code, so that the
  output can be compared byte for byte.  At present the only such fix is
  the stack-pointer save and restore in `TMPSUB.ASM`, 25 bytes that DRI
  had commented out.

```
python3 tools/verify_dri.py
```

builds both releases with `--dri-exact` and compares each `.SPR` against
the matching reference:

```
MP/M II V2.0:
  XDOS      identical to DRI 2.0
  BNKXDOS   identical to DRI 2.0
  RESBDOS   identical to DRI 2.0
  TMP       identical to DRI 2.0
MP/M II V2.1:
  XDOS      identical to DRI 2.1
  BNKXDOS   identical to DRI 2.1
  RESBDOS   identical to DRI 2.1
  TMP       identical to DRI 2.1
```

The comparison covers the program image and the relocation bits that
describe it.  It stops at the end of the program: DRI's linker left
stale bytes in the tail of the bitmap, which no loader reads and no
assembler can be made to reproduce.

## What changed between the releases

Every file in the distribution that differs, with the source it is built
from:

| File          | Bytes | Source                    | Reconstructed |
|---------------|-------|---------------------------|---------------|
| `XDOS.SPR`    | 226   | `NUCLEUS`                 | yes, byte-exact |
| `RESBDOS.SPR` | 47    | `NUCLEUS`                 | yes, byte-exact |
| `BNKXDOS.SPR` | 6     | `NUCLEUS/BNKXDOS.ASM`     | yes, byte-exact |
| `TMP.SPR`     | 23    | `NUCLEUS/TMPSUB.ASM`      | yes, byte-exact |
| `MPMLDR.COM`  | 1     | `MPMLDR/MPMLDR.PLM`       | yes |
| `SHOW.PRL`    | 2     | `UTIL4/SHOW.PLM`          | yes |
| `PRINTER.PRL` | 23    | `UTIL5/PRINT.PLM`         | yes |
| `SCHED.RSP`   | 1     | `UTIL2/SCRSP.PLM`         | yes |
| `SDIR.PRL`    | 2     | `UTIL7`                   | no, see below |
| `SPOOL.PRL`   | 88    | `UTIL5/MSPL.PLM`          | no, see below |
| `SPOOL.BRS`   | 37    | `UTIL2/SPBRS.PLM`         | no, see below |
| `PIP.PRL`     | 64    | `UTIL6`                   | no, see below |
| `GENSYS.COM`  | 47 + 768 longer | `MPMLDR/GENSYS.PLM` | no, see below |
| `BNKBDOS.SPR` | 568   | `BNKBDOS/BNKBDOS.ASM`     | already V2.1, see below |
| `LIB.COM`     | 2     | none (DRI tool)           | serial only |
| `LINK.COM`    | 16    | none (DRI tool)           | n/a |
| `RMAC.COM`    | 2     | none (DRI tool)           | serial only |

The four transients built from PL/M cannot be checked byte for byte in
any case: `uplm80` is not Digital Research's PL/M-80 and generates
different code.  The nucleus is all assembler, which is why it can be.

## The nucleus changes

V2.1 is a field patch that was folded back into the sources.  Every
nucleus module has the same program length in both releases, and each
change is either an in-place rewrite of the same number of bytes or a
branch into an area V2.0 had reserved and left full of zeros:
`PATCH.ASM`'s 128 bytes, `CONBDOS.ASM`'s 80, `TMPSUB.ASM`'s 128, and -
when those ran out - the tail of `pdtbl` entry 0 in `DATAPG.ASM` and
three unused slots of `ProcAddressTable` in `BNKXDOS.ASM`.

### VER.ASM

Version word `0120h` to `0121h`, banner `V2.0` to `V2.1`, copyright 1981
to 1982, revision date `09/14/81` to `02/01/82`.  `DATAPG.ASM`'s
time-of-day epoch moves with it: day 1353 to 1493, which is the same two
dates counted from 1 January 1978.

### XDOS: the patch area (PATCH.ASM)

Five routines, filling the reserved 128 bytes exactly:

* `pdisksel` - called from `MPM.ASM` where the TMP's process descriptor
  is built.  V2.0 wrote the console number into `tmp$pd.disk$slct`; V2.1
  puts the system drive, `system$data(123)-1`, in the high nibble.
* `flshmx` - called from `XDOS.ASM` in the terminate path.  The `CALL
  RLSMX` that V2.0 made from the dispatcher is gone (five bytes of `NOP`
  in `DSPTCH.ASM` now); the MX queues a process owns are released here
  instead, before its console buffer is flushed, so that a process
  waiting on one of them is let go earlier.
* `cliattr` - called from `CLI.ASM` where the loaded program's process
  descriptor is filled in.  `pd(1dh)` collects the f1'..f4' attribute
  bits of the command's FCB.  `system$data(96)`, unassigned in V2.0,
  turns it on.  `XDOS.ASM` no longer clears `pd(1dh)` on load - two
  `NOP`s where an `INX H`/`MOV M,A` used to be.
* `clidrive` - called from `CLI.ASM` instead of jumping straight to the
  retry on the system disk; it remembers the drive first.
* `clinofile` - called from `CLI.ASM` on 'No such file.'; it gives the
  memory segment back before printing.

### XDOS: the abort stack pointer (XDOS.ASM, DATAPG.ASM)

Planting a process's abort return address has to account for the process
running in a bank.  The replacement reads `pd.memseg` and, when
`system$data(3)` (add system call user stacks) says so, takes the stack
from `system$data(80 + 2*(memseg-2))` rather than from `pd.stkptr`; it
also leaves four bytes rather than two.  The 37 bytes of code did not
fit anywhere in XDOS, so DRI put them in the tail of `pdtbl` entry 0 -
the `db 0ffh` and `ds 36` that entry ends with - which is never read
before the entry is filled in.

### XDOS: the list number's low nibble (TH.ASM, BNKXDOS.ASM)

The console/list byte of a process descriptor carries the list device in
its high nibble, so the terminal handler has to mask before comparing.
`DCX B`/`LDAX B`/`MOV C,A` becomes a three-byte `CALL`, and the masking
sequence - the same three instructions plus `ANI 0FH` and `RET` - goes
into `ProcAddressTable` entries 2 to 4 of the banked XDOS, which is
copied into the resident one.  It is the only place in either module
with six spare bytes.

### RESBDOS

* `extjmptbl`'s `xprint` and `xcrlf` entries now go through
  `attxprint`/`attxcrlf` in the patch area, which call `testcnsatt`
  first: the XIOS may only print through them when the calling process
  owns the console.
* the direct console i/o function's status calls go through `kbconst`,
  which reports a character that was read ahead and pushed back as
  ready before asking the XIOS.
* the disk function copy table gains `fcbout` for functions 19 (delete
  file), 23 (rename file) and 30 (set file attributes), so the FCB is
  copied back out as well as in.
* `comparerecs` jumps past `shellerr`'s `LHLD ARET`, which was
  overwriting the error code it had just put in HL.

### TMP

* a submit file that has already been started stays in force even when
  the submit flag for the console has been cleared - the patch area
  checks `fcb.nr` before giving up.
* any non-zero return from read random is a disk error, not just `0ffh`.

## The transients

### SHOW, PRINTER, SCHED, MPMLDR - reconstructed

* `SHOW.PLM`: `declare user(15) byte` becomes `user(16)`.  User numbers
  run 0 to 15, and `last(user)` was one short, so `SHOW [USERS]` never
  listed user 15.  Both changed bytes are that constant.
* `PRINT.PLM`: a list number is valid up to `system$data(197)`, the
  number of printers the system was generated with, instead of a fixed
  sixteen.
* `SCRSP.PLM`: the last character of the scheduler's process name gets
  its high bit set, marking it a system process the way Spool, MPMSTAT
  and Abort already were.
* `MPMLDR.PLM`: banner only.

### SDIR, SPOOL, PIP, GENSYS - identified, not reconstructed

The evidence, for whoever picks this up.  Offsets are into the `.PRL`
file, which for these is also the load address.

`SDIR.PRL`, two bytes:

* header byte 5, the GENMOD minimum-buffer field, `0000` to `1000`: V2.1
  asks MP/M for 4K more memory than its image.
* `23C1`: `LHLD 3BADH` becomes `LHLD 3BB1H`, inside
  `if mem16(3BB3H) >= mem16(3BADH) + 46`.  Three pointers live at
  `3BAD`, `3BB1` and `3BB3`; the bounds check was reading the first
  where it wanted the third's neighbour.

`SPOOL.PRL`, 88 bytes: DRI reused the message text
`"- Enter ATTACH SPOOL to re-attach console to spooler"` at `012A`, and
the start of `"*** Spooler detac..."`, as a patch area, and turned three
inline sequences at `025F`, `026D` and `031A` into calls into it.
`SPOOL.BRS` changes 37 bytes to match.

`PIP.PRL`, 64 bytes in seven places, the largest at `1FE1-1FF8` and
`202E-203D`.

`GENSYS.COM` is the odd one out: 8704 bytes in V2.0 and 9472 in V2.1, so
it is a recompile rather than a patch.  It is the natural place for
whatever writes `system$data(96)`, the byte the CLI's new attribute
handling is gated on.

### BNKBDOS - the shipped source is already V2.1

`mpm2src/BNKBDOS/BNKBDOS.ASM` builds, byte for byte, the V2.1
`BNKBDOS.SPR`, not the V2.0 one - DRI put the newer banked BDOS in the
source release.  So there is nothing to recover: `--version 2.0` builds
the V2.1 banked BDOS, as this repository always has.

Going the other way would mean reconstructing the *older* code from
DRI's V2.0 binary across 26 separate regions, including 271 bytes of
patch routines that moved wholesale from around `05xx-08xx` to `22xx`.
That is undoing bug fixes rather than recovering sources, so it has not
been done.

## Loose ends found on the way

* `bin/dri/TMP.SPR` was this repository's own build, not Digital
  Research's - 1536 bytes where every DRI copy is 1408.  Replaced with
  `mpm2_external/mpm2dist/TMP.SPR`.
* `SCHED.RSP`, `SPOOL.RSP` and `MPMSTAT.RSP` are built here from two
  modules each (`*BRS.PLM` and `*RSP.PLM`).  DRI's `SCHED.SUB` builds
  `*.RSP` from `*RSP.PLM` alone and `*.BRS`, a separate banked-RSP file
  this repository does not produce at all, from `*BRS.PLM` plus
  `BRSPBI.ASM`.  The merged build puts the process descriptor somewhere
  other than the start of the image, where MP/M expects it; `SCHED`
  reports "Resident portion of scheduler is not in memory" on both
  releases because of it.  Not a V2.1 issue, and not fixed here.
