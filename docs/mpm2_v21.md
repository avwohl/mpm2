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
  output can be compared byte for byte, or at least does what DRI
  shipped.  It defines `DRIEXACT` for both tools.  At present there are
  two such fixes: the stack-pointer save and restore in `TMPSUB.ASM`, 25
  bytes that DRI had commented out, and, in V2.1's `PIP.PLM`, MULTCOPY's
  file-not-found test, which DRI's patch (`PIP.PRL` 1FE1) cut down to
  the low byte of `NCOPIED` - so a copy that matched a multiple of 256
  files ends in FILE NOT FOUND.  The default build tests the whole word,
  as V2.0 did.

```
python3 tools/verify_dri.py
```

builds both releases with `--dri-exact` and compares the nucleus and
UTIL1's assembler and debugger against the matching reference:

```
MP/M II V2.0:
  XDOS.SPR     identical to DRI 2.0
  BNKXDOS.SPR  identical to DRI 2.0
  RESBDOS.SPR  identical to DRI 2.0
  TMP.SPR      identical to DRI 2.0
  ASM.PRL      identical to DRI 2.0 but for 11 bytes no source sets
  RDT.PRL      identical to DRI 2.0
  DDT.COM      identical to DRI 2.0
MP/M II V2.1:
  XDOS.SPR     identical to DRI 2.1
  BNKXDOS.SPR  identical to DRI 2.1
  RESBDOS.SPR  identical to DRI 2.1
  TMP.SPR      identical to DRI 2.1
  ASM.PRL      identical to DRI 2.1 but for 11 bytes no source sets
  RDT.PRL      identical to DRI 2.1
  DDT.COM      identical to DRI 2.1
```

For an `.SPR` the comparison covers the program image and the
relocation bits that describe it.  It stops at the end of the program:
DRI's linker left stale bytes in the tail of the bitmap, which no loader
reads and no assembler can be made to reproduce.  `ASM.PRL`, `RDT.PRL`
and `DDT.COM` are compared whole, header and bitmap included; the bytes
of `ASM.PRL` it lets through are explained under
[ASM, RDT and DDT](#asm-rdt-and-ddt---no-change).  `CONTROL` and
`mpm2dist` carry the same three files, so the V2.0 and V2.1 references
for them are one and the same.

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
| `SPOOL.PRL`   | 88    | `UTIL5/MSPL.PLM`          | yes |
| `SDIR.PRL`    | 2     | `UTIL7/DSE.PLM`, `tools/build.py` | yes, see below |
| `SPOOL.BRS`   | 37    | `UTIL2/SPBRS.PLM`         | yes, see below |
| `PIP.PRL`     | 64    | `UTIL6/PIP.PLM`           | yes, see below |
| `GENSYS.COM`  | 47 + 768 longer | `MPMLDR/GENSYS.PLM`, `LDRLWR.ASM` | yes, see below |
| `BNKBDOS.SPR` | 568   | `BNKBDOS/BNKBDOS.ASM`     | already V2.1, see below |
| `LIB.COM`     | 2     | none (DRI tool)           | serial only |
| `LINK.COM`    | 16    | none (DRI tool)           | n/a |
| `RMAC.COM`    | 2     | none (DRI tool)           | serial only |

The four transients built from PL/M cannot be checked byte for byte in
any case: `uplm80` is not Digital Research's PL/M-80 and generates
different code.  The nucleus is all assembler, which is why it can be.

`ASM.PRL`, `RDT.PRL` and `DDT.COM` are not in the table: `CONTROL`'s and
`mpm2dist`'s are the same file.  The copies next to their sources in
`mpm2src/UTIL1`, which neither master carries, differ from it, but only
in bytes no source sets; see
[ASM, RDT and DDT](#asm-rdt-and-ddt---no-change).

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

### SHOW, PRINTER, SCHED, SPOOL, MPMLDR - reconstructed

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
* `MSPL.PLM`, four changes.  The spooler drops its priority to 201
  before it starts listing, so that it runs in the background.  The pass
  that checks the files exist opens them with `f5'` set, the way the
  pass that lists them already did.  That same pass now recognises the
  `[D]` option instead of trying to open it as a file and stopping with
  `Can't Open File = D`, which is what command tails like
  `SPOOL A.TXT[D]` used to do.  And the message it prints on detaching
  loses its last two lines - DRI took the bytes for the patch code.

The first two are one statement each and read straight off the patch.
The third is a source-level reconstruction of a hand-written patch: DRI
jumped back to the top of the loop, which PL/M cannot express, so the
loop body is restructured as an `if`/`else` around the open.  It does
what the patch does; it does not assemble to the same bytes, and neither
does anything else here that is compiled rather than assembled.

### SDIR, SPOOL.BRS, PIP, GENSYS

These were first identified here and left for later; all four have since
been recovered into `src/overrides`, each behind `$if MPM21`, and the
commit that did each has the evidence in full.  Offsets are into the
`.PRL` or `.COM` file, which for these is also the load address.

`SDIR.PRL`, two bytes.  `23C1`, `LHLD 3BADH` becomes `LHLD 3BB1H`:
`store$file$info` in `UTIL7/DSE.PLM` tests `last$f$i$adr` instead of
`f$i$adr` (which is always zero there) before it adds a record, so the
file table is bounded at last.  And header byte 5, GENMOD's extra-memory
word, `0000` to `1000`: V2.1 asks MP/M for 4K more, room for about 180
more records.  `tools/build.py` gives the V2.1 build the 4K and V2.0
none.

`SPOOL.BRS`, 37 bytes, the banked resident half of the spooler
(`UTIL2/SPBRS.PLM`): the spooler detaches from the console of the last
request before it waits on SPOOLQ for the next.  The build makes `.BRS`
files now (`tools/build.py`), and `gensys.sh` puts them in every system.

`PIP.PRL`, 64 bytes in seven places, five changes: `[A]` no longer turns
a file copy into a character copy; `[O]` counts in a file to file copy;
a multiple-file copy without `[A]` no longer runs `archck`, and clears
both extent bytes; `[K]` also suppresses MULTCOPY's closing new line;
and an error no longer closes and deletes the destination's scratch
file.  The room for the `[K]` test came from the file-not-found test
above it, which the patch cut down to the low byte of `NCOPIED`
(`1FE1`); the default build keeps the whole word, `--dri-exact` builds
DRI's test.

`GENSYS.COM` is V2.0's with a 768-byte patch area, not a recompile.  It
asks "Enable Compatibility Attributes (N) ?" (system data byte 96, which
the V2.1 CLI's `cliattr` tests), shows a temporary or system drive P: as
`P:` instead of `@:`, limits the user memory segments to seven, and
closes each SPR, RSP and BRS file it has loaded.  `MPM.SYS` itself is
generated by `tools/gensys.py`, which writes byte 96 from
`build_all.sh --compat-attributes` (default no, as DRI's).

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

### ASM, RDT and DDT - no change

The assembler and the debugger are MAC sources that Digital Research
never linked.  `UTIL1/ASM.SUB` and `DDT.SUB` assemble each module twice,
the second time with MAC's `+R` toggle (`mac ddt1asm $pzsz+r`), which
puts every ORG 100H higher - what the Programmer's Guide (4.4.1) tells a
user to do by hand to make a PRL.  PIP joins the two HEX files and GENMOD
compares the copies: a byte one more in the second is the high byte of
an address and gets a bit in the relocation map, and `$Z` makes it skip
a byte that is zero in the second copy.

* `ASM.PRL`: the seven `AS*.ASM` modules, each at its own ORG (100H,
  200H, 1100H, ... 1BA0H), `genmod asm.hex asm.prl $1000`.
* `RDT.PRL`: `DDT1ASM` (ORG 0) and `DDT2MON` (ORG 0 too, then `DS 680H`
  over `DDT1ASM`'s space, so its code starts at 0680H) are GENMOD'd
  into `RELDDT`, the debugger as a relocatable module with its own map.
  GENHEX puts it at 100H, so its header page is at 100H and the module
  at 200H, and `DDT0MOV`, the relocator, is loaded over the header page:
  its first instruction is a lone `LXI B` opcode at 100H, whose operand
  is the module length GENMOD left at 101H.  At run time it moves the
  module under the BDOS and relocates it with the module's map.
  `genmod relddt.hex rdt.prl $z1500` makes the PRL - `$Z` because the
  second copy of the header page is zeros where the first has
  `DDT0MOV`'s code.
* `DDT.COM`: `prlcom rdt.prl ddt.com` - RDT.PRL's image, without the
  header or the map.

`tools/build.py` does the same (`genmod=True` targets): `um80 --aseg`
assembles each module as it is and a copy with every ORG 100H higher,
and `tools/genmod.py` does what GENMOD, GENHEX and PRLCOM did.  Under
cpmemu, DRI's own `MAC.COM` gives the same HEX records for all ten
modules both ways.  `genmod.py` refuses the two kinds of ORG that um80
does not assemble as MAC does, in either copy, rather than let them
through: an ORG in column 1, which MAC takes for an ORG and um80 does
not, and a label on an ORG line, which MAC sets to the new location
and um80 to the old.  None of the ten modules has either.

The code is the same in V2.0 and V2.1, and so are the files: the V2.0
master (`CONTROL`) and V2.1 (`mpm2dist`) carry byte for byte the same
`ASM.PRL`, `RDT.PRL` and `DDT.COM`.  GENMOD loads the first copy at
0700H and never clears memory, so a DS area, or the gap before a
module's ORG, keeps whatever the program before it left there - in the
shipped files, MAC.COM, as MAC leaves memory after assembling `AS3SYM`:
with a memory image saved by cpmemu after `mac as3sym`, GENMOD
reproduces all three exactly.

The default build leaves those bytes zero.  `--dri-exact` loads MAC.COM
from `UTIL9` into GENMOD's memory, which makes `RDT.PRL` and `DDT.COM`
identical to DRI's, and `ASM.PRL` identical except for 11 bytes of
MAC's variables, which MAC changed while it ran.  `verify_dri.py`
allows exactly those, in both releases.

The copies next to the sources in `mpm2src/UTIL1` are not a reference.
They are a rebuild in the source tree that neither master carries, and
they differ from the shipped files in 3141 (`ASM.PRL`), 77 (`RDT.PRL`)
and 77 (`DDT.COM`) bytes, every one of them a byte no source sets.
There GENMOD's memory held PIP.COM - the CP/M PIP in `UTIL9`, which the
submit files run just before GENMOD - over MAC.COM.  All 78 such bytes
of `RELDDT` and 3154 of `ASM` are PIP.COM's, byte for byte, 29 more of
`ASM`'s are MAC.COM's from above PIP's end, and the other 34 are what
PIP left in its storage above its code, most of it pieces of the HEX
text it had been copying (`61DC54623E5CD48EC` CR LF at 1B8D).

The build used to link the three with `ul80`, which made none of them
right.  `RDT.PRL` was the bare module, 1194H bytes, with no relocator
and an empty relocation map, and `DDT.COM` started with `DDT1ASM`'s
`JMP 0683H` at 100H (the pending ul80 now stops there: "DDT1ASM is
assembled at 0000H and was loaded over DDT0MOV's relocator").
`ASM.PRL` had the right code but not one relocation bit - it ran only
in a memory segment based at 0000H - and five bytes of `AS6MAIN`'s
closing DS written out as zeros.

MP/M II's DDT will not run under a CP/M emulator: it asks the BDOS for
its version and warm boots unless it is MP/M, and it finds the
breakpoint vector through the system data page.

## Loose ends found on the way

* `bin/dri/TMP.SPR` was this repository's own build, not Digital
  Research's - 1536 bytes where every DRI copy is 1408.  Replaced with
  `mpm2_external/mpm2dist/TMP.SPR`.
* `SCHED.RSP`, `SPOOL.RSP` and `MPMSTAT.RSP` were built here from two
  modules each (`*BRS.PLM` and `*RSP.PLM`), which put the process
  descriptor somewhere other than where MP/M looks for it.  They are now
  built the way DRI's `SCHED.SUB` does it: `*.RSP` from `*RSP.PLM`
  alone and a separate `*.BRS` from `*BRS.PLM` with
  `src/brs_runtime.mac` in `BRSPBI.ASM`'s place, and `gensys.sh` loads
  all four of DRI's resident system processes into every system.
* SUBMIT and SPOOL (when there is no SPOOL RSP and `SPOOL.PRL` prints
  the files itself) build their buffers from their last variable up to
  the top of the memory segment - `rbuff` at `minimum$buffer`, `buffer` at
  `dummy$buffer`.  That relies on Intel's layout, which put the stack
  below the data and nothing after the last variable.  uplm80 up to 0.3.6
  put its string constants, the procedures' shared locals (`??AUTO`) and
  the stack after the last variable, so a command file over 1K came out
  garbled ("Bad entry"), and the spooler lost its buffer pointer, which
  is one of those locals, a few records in; for a while the overrides
  moved both buffers to `.MEMORY`.  uplm80 0.3.7 lays a program out as
  Intel's PL/M-80 does, variables last, and both build from DRI's text.
* ul80 0.3.48 does not relocate `__END__` in a `.PRL`: a reference to it
  is not marked in the bit map, so `.MEMORY` is right only when the
  program is loaded at a segment base of 0000H.  PIP, ED, SDIR, STAT,
  SUBMIT and SPOOL use it.  Every memory segment `gensys.sh` generates
  starts at 0000H (seven banks of 0000-BFFFH), so nothing here shows it;
  a system with a segment based elsewhere would.  Reproduction:
  `extrn __END__` / `ld hl,__END__` linked with `ul80 --prl` leaves the
  bit for the high byte clear.
* The emulator's SFTP RSP ran in the BDOS's default error mode.  A read
  refused because a console program had the file open ("File Currently
  Open") was then reported on the RSP's console, and V2.0's RESBDOS
  prints a BDOS error straight through the XIOS with the process's whole
  console byte - the RSP's is 0F0H, console 0 and list device 15.  The
  XIOS polled device 0E0H for console 0F0H's output to be ready, which
  it never is, inside the BDOS, and every process that touched a disk
  stopped behind it.  V2.1's RESBDOS (`attxprint`) masks the byte and
  prints only for a process that owns the console, so a V2.1 system
  just lost the message.  The RSP now selects return-error mode (BDOS
  function 45) when it starts, and opens the files it reads in
  read-only mode.
