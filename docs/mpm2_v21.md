# MP/M II V2.1

`mpm2src.zip` is the V2.0 source release.  Digital Research never put the
V2.1 sources on a distribution disk, and no archive has them: bitsavers,
the Tim Olmstead collection, cpm.z80.de and the Unofficial CP/M Web Site
all carry the same two files, `mpm2src.zip` (sources, V2.0) and
`mpm_ii.zip` (binaries, V2.1).  What follows is a reconstruction of V2.1
from the binaries, checked against them: byte for byte where the code is
assembled, and by what it does where it is compiled from PL/M.

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
| `MPMLDR.COM`  | 3     | `MPMLDR/MPMLDR.PLM`       | yes |
| `SHOW.PRL`    | 2     | `UTIL4/SHOW.PLM`          | yes |
| `PRINTER.PRL` | 23    | `UTIL5/PRINT.PLM`         | yes |
| `SCHED.RSP`   | 1     | `UTIL2/SCRSP.PLM`         | yes |
| `SPOOL.PRL`   | 88    | `UTIL5/MSPL.PLM`          | yes |
| `SDIR.PRL`    | 2     | `UTIL7/DSE.PLM`, `tools/build.py` | yes, see below |
| `SPOOL.BRS`   | 37    | `UTIL2/SPBRS.PLM`         | yes, see below |
| `PIP.PRL`     | 64    | `UTIL6/PIP.PLM`           | yes, see below |
| `GENSYS.COM`  | 47, and 768 longer | `MPMLDR/GENSYS.PLM`, `LDRLWR.ASM` | yes, see below |
| `BNKBDOS.SPR` | 568   | `BNKBDOS/BNKBDOS.ASM`     | already V2.1, see below |
| `LIB.COM`     | 2     | none (DRI tool)           | serial only |
| `LINK.COM`    | 16    | none (DRI tool)           | n/a |
| `RMAC.COM`    | 2     | none (DRI tool)           | serial only |

The byte counts include the two bytes of serial number in the files that
carry one: `XDOS.SPR`, `RESBDOS.SPR`, `MPMLDR.COM`, `GENSYS.COM`,
`LIB.COM`, `LINK.COM` and `RMAC.COM`.

None of the transients can be checked byte for byte: they are compiled
from PL/M, and `uplm80` is not Digital Research's PL/M-80 and generates
different code.  Each one was checked instead against the patch,
instruction by instruction, and the larger ones also by running them
beside DRI's binary on the same system with the same input; the sections
below say how.  The
nucleus is all assembler, which is why it can be compared byte for byte.

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
* `MPMLDR.PLM`: banner only; the other two bytes are the serial number.
* `MSPL.PLM`, four changes.  The spooler drops its priority to 201
  before it starts listing, so that it runs in the background.  The pass
  that checks the files exist opens them with `f5'` set, the way the
  pass that lists them already did.  That same pass now recognises the
  `[D]` option instead of trying to open it as a file and stopping with
  `Can't Open File = D`, which is what command tails like
  `SPOOL A.TXT[D]` used to do.  And the message it prints on detaching
  loses its last two lines - DRI took the bytes for the patch code.

Of the spooler's four changes, the first two are one statement each and
read straight off the patch.  The third is a source-level reconstruction
of a hand-written patch: DRI jumped back to the top of the loop, which
PL/M cannot express, so the loop body is restructured as an `if`/`else`
around the open.  It does what the patch does; it does not assemble to
the same bytes, and neither does anything else here that is compiled
rather than assembled.

The four sections that follow are the transients whose changes were
larger than a statement.  Each is behind `$if MPM21` in
`src/overrides`.  Offsets into a `.PRL` are file offsets, which for a
transient linked at 0100H are also its load addresses.

### SPOOL.BRS - the spooler detaches first (SPBRS.PLM)

`SPOOL.BRS` is the banked half of the resident spooler.  The 37 bytes
are 32 of code at `01C2-01E1` of the program (`02C2` in the file, after
the header page) and the five bitmap bytes that describe them.  The
program length, `07ED`, and everything after `01E2` are unchanged: it is
an in-place patch of the top of the spooler's main loop.

```
V2.0                              V2.1
LXI D,0368H  ; .spool$uqcb        MVI C,93H     ; 147, detach console
MVI C,89H    ; read$queue         CALL 02FCH    ; mon1
CALL 02FCH   ; mon1               LXI D,0368H
LXI B,0016H  ; disk$slct          MVI C,89H     ; read$queue
LHLD 033CH   ; spool$pd$adr       CALL 02FCH
DAD B / LDA 0370H / MOV M,A       LXI B,000EH   ; console
LXI B,000EH  ; console            LHLD 033CH / DAD B / LDA 0371H / MOV M,A
LHLD 033CH / DAD B                LXI B,0008H / DAD B  ; 0E+08 = disk$slct
LDA 0371H / MOV M,A               LDA 0370H / MOV M,A
LXI D,0000H                       NOP
```

In the source that is `call mon1 (detach,0)` put in front of `call mon1
(read$queue,.spool$uqcb)`, so the spooler lets go of the console of the
last request before it waits on SPOOLQ for the next.  DRI found the five
bytes it needed by storing `console` before `disk$slct`, so that the
second store reuses the pointer, and by dropping the `LXI D,0` in front
of `detach$list`; neither detach reads DE.  The reconstruction keeps
V2.1's store order.  Built with `--version=2.1`, the loop compiles to
detach (93H), read$queue, the console at pd+0EH, `disk$slct` at pd+16H
and detach$list; `spool file[d]` lists the file and deletes it, and
after STOPSPLR the next listing stops at its first line and leaves the
file.

No other resident system process file differs between the two masters
apart from `SCHED.RSP`'s one byte (above).  The copy of `SCHED.RSP` in
`mpm2src/UTIL2`, a rebuild in the source tree, also differs from both
masters at `0118-0135`: the descriptor's undefined tail, whatever was in
memory when it was built.

### SDIR - the file table's bounds check (DSE.PLM)

SDIR's two bytes are two halves of one fix.

In the code, one operand changes, at `23C1`:

```
23B6  CALL 22E0H        hash$look$up
23B9  RAR
23BA  JC   2452H        found: go and update the existing record
23BD  LXI  D,002EH      2 * size(file$info)
23C0  LHLD 3BADH        f$i$adr            V2.1: LHLD 3BB1H, last$f$i$adr
23C3  DAD  D
23C4  LXI  D,3BB3H      .x$i$adr
23C7  CALL 3A39H        HL = x$i$adr - HL
23CA  JNC  23D0H
23CD  MVI  A,00H        return false: out of memory
23CF  RET
23D0  LXI  D,0017H      size(file$info)
23D3  LHLD 3BB1H
23D6  DAD  D
23D7  SHLD 3BB1H        last$f$i$adr := last$f$i$adr + 23
23DA  SHLD 3BADH        f$i$adr = ...
```

This is `store$file$info` in `UTIL7/DSE.PLM`:

```
if not hash$look$up then
do;
    if f$i$adr + 2 * size(file$info) > x$i$adr then     /* V2.1: last$f$i$adr */
        return(false);                     /* out of memory         */
    f$i$adr = (last$f$i$adr := last$f$i$adr + size(file$info));
```

The words are identified by what the code does with them:

* `23D0-23DA` above makes `3BB1` `last$f$i$adr` and `3BAD` `f$i$adr`.
* `get$files` (`259E-25CF`) loads `3BB1` from `3BAF`, and `3BB3` from
  `0006H`, maxb.
* `DM.PLM`'s main line sets `3BAF` to `3D88H` at `06E8`.  That is
  `.hash$table + size(hash$table) - size(file$info)`, with `.memory` at
  `3C9FH`.

So `3BAD` is `f$i$adr`, `3BAF` `end$adr`, `3BB1` `last$f$i$adr` and
`3BB3` `x$i$adr`, which is DSE's own declaration order.  The XFCB branch
further down (`2460`) already tested `last$f$i$adr`.

The V2.0 test could never fail.  `hash$look$up` returns false only after
following the hash chain to its end, that is with `f$i$adr = 0`, so the
test read `0 + 46 > x$i$adr`.  Nothing else bounds the table.  Once a
drive has more matching files than fit between `.memory+256` and the top
of SDIR's memory segment, the 23-byte records run over the `JMP` to the
XDOS that the CLI puts in the segment's top three bytes, and on into
memory the process does not own.  SDIR checks the console after every
directory entry, so the next BDOS call jumps into a file name.

The other byte is the high byte of the `.PRL` header's extra-memory word
(bytes 4-5, GENMOD's third argument), `0000H` to `1000H`.  V2.0's
`UTIL7/SDIR.SUB` gives GENMOD no third argument, and every other `.PRL`
carries the same figure in both releases.  The CLI sizes a segment as
`high(len + len/8 + 0FFH) + high(extra + 0FFH) + 1` pages, and MP/M
gives a program the smallest free segment that fits.  V2.0 SDIR asks for
45H pages and can land in a segment with room for about 80 files; V2.1
asks for 55H.  The extra memory is not what SDIR's hash table at
`.memory` needs - the pages of the relocation bitmap, dead once the
image is relocated, always leave room for that - so on its own it only
moves the point where V2.0 overruns.  `tools/build.py` links SDIR with
1000H for `--version 2.1` and none for 2.0.

This was checked on the emulator, with the `MPM.SYS` segment table
patched to user segments of 48H, 58H and 68H pages and C0H for the rest:

| SDIR                          | segment | files | result |
|-------------------------------|---------|-------|--------|
| DRI V2.0                      | 48H     | 115   | lists them ("Not enough memory for sort") |
| DRI V2.0                      | 48H     | 116   | hangs the system (2 of 3 runs), or `Bdos Err On B: Bad Sector` |
| DRI V2.1                      | 58H     | 200   | lists them, sorted |
| DRI V2.1                      | 58H     | 400   | "Out of Memory" at 293, lists those, returns |
| source V2.1                   | 68H     | 400   | "Out of Memory" at 311, lists those, returns |
| source V2.0 (any extra)       | 58H/68H | 200/400 | hangs the system |

Record 116 is the first to reach DRI V2.0's jump at `47FDH`, and 293 and
311 are the counts the V2.1 test allows in those segments.  Apart from
where it stops and the totals, the source-built V2.1 listing is line for
line DRI's.  That needs uplm80 0.3.7 or later, which computes `x MOD 0`
as PL/M-80 does: with an earlier uplm80 every source-built SDIR repeats
its heading on each line, because SDIR's default page length is 0.

Built with `--version=2.1`, `SDIR.PRL` differs from the V2.0 build in the
same single operand (`LHLD FIADR` becomes `LHLD LASTFIADR`) and in the
header.

### PIP - five changes in seven places (PIP.PLM)

`PIP.PRL` differs in 64 bytes: 57 in the program, in seven places, and 7
in the relocation bitmap that goes with them.  DRI patched every change
in place over V2.0's code, so nothing moves, and the addresses give the
variables' names once `PIP.PLM`'s data area is mapped.  PL/M-80
allocates that area in declaration order, starting with `COLUMN` at
`2251H`, just after the 100-byte stack.

| Address | Variable |
|---------|----------|
| `2262H` | `SCOM` - the source is a `.COM` file |
| `243AH` | `OBJ`, `CONT(14)` - the `[O]` option |
| `242CH` | `ARCHIV`, `CONT(0)` - `[A]` |
| `2436H` | `KILDS`, `CONT(10)` - `[K]` |
| `2270H` | `source.fcb(12)`, the source's extent number |
| `22CCH` | `odest.fcb(12)`, the destination's |
| `2494H` | `I`, `SIMPLECOPY`'s loop index |
| `2499H` | `NCOPIED`, local to `MULTCOPY` |

The seven places:

| Offset | Where | V2.0 | V2.1 |
|--------|-------|------|------|
| `06E7` | main, concatenation loop | `LDA 243AH` / `LXI H,2262H` / `ORA M` / `MOV M,A` | `LDA 2262H` / `LXI H,243AH` / `ORA M` / `MOV M,A` |
| `0B12` | `ERROR` | `CALL SETDUSER`, `CALL CLOSE(.dest)`, `CALL DELETE(.dest)`; `CLOSE(.odest)` follows at `0B21` | `JMP 0B27H`; at `0B15` a new routine `LXI H,2270H` / `MVI M,0` / `LXI H,22CCH` / `MVI M,0` / `RET` |
| `1258` | `GETSOURCEC` | `LDA 2262H` / `RAR` / `JNC` | `LDA 243AH` / `RAR` / `JNC` |
| `1855` | `RD$EOF` | `LDA 2262H` / `RAR` / `JNC` | `LDA 243AH` / `RAR` / `JNC` |
| `1EBE` | `SIMPLECOPY` | `MVI M,00H` into `2494H` | `MVI M,01H` |
| `1FE1` | `MULTCOPY`, nothing left to copy | 16-bit `NCOPIED = 0` test, then `CALL CRLF` | 8-bit test, then `LDA 2436H` / `RAR` / `RC` before `CALL CRLF` |
| `202E` | `MULTCOPY`, archive check | `CMA` / `PUSH PSW` / `CALL ARCHCK` / ... / `ORA C` / `RAR` / `JNC`, then `MVI M,0` into `22CCH` | `ORA A` / `JZ 203AH` / `CALL ARCHCK` / `ORA A` / `JZ`, then `CALL 0B15H` |

In PIP's terms these are five changes:

* **`[A]` no longer turns a file copy into a character copy.**
  `SIMPLECOPY` looks through the options to decide whether it can copy
  the file directly.  That loop now starts at 1 instead of 0, so
  `CONT(0)`, the archive option, no longer turns off the fast copy.  In
  V2.0, `PIP B:X.DAT=A:X.DAT[A]` copied a binary file only as far as its
  first ctl-Z.
* **`[O]` works in a file-to-file copy.**  `GETSOURCEC` and `RD$EOF` test
  `OBJ` instead of `SCOM`.  The concatenation loop's `SCOM = SCOM OR OBJ`
  becomes `OBJ = OBJ OR SCOM`, so a `.COM` source still counts as binary
  there.  DRI shipped the other side of this as well: a `.COM` file
  copied file-to-file with a character option and no `[O]` now stops at
  a ctl-Z too.
* **An archived file is copied whole.**  V2.0 wrote `if not archiv or
  archck`.  PL/M evaluates both sides of an `OR`, so `archck` searched
  every extent of every file even without `[A]`.  When all of a file's
  extents were marked archived, it left `source.fcb(12)` at the extent of
  the last directory entry it read, so `PIP B:=A:*.*` copied such a file
  starting from that extent.  V2.1 skips `archck` unless `[A]` was given,
  and clears both extent numbers before the copy.
* **`[K]` also drops the new line** that a multi-file copy prints at the
  end.
* **`ERROR` no longer cleans up the destination.**  It no longer closes
  and deletes the destination's `.$$$` file, or closes the original
  destination.  DRI jumped over those four calls to find the twelve
  bytes for the new routine at `0B15`.  As a result, a copy that fails
  after the destination was made - `PIP C:NEW.TXT=B:NOSUCH.TXT`, for
  example - leaves `C:NEW.$$$` behind.  The bytes do not say why the
  cleanup was dropped.  A likely reason is that most errors happen before
  any destination exists, so the calls were working on whatever `dest`
  and `odest` held.  DRI's later CP/M 3 PIP keeps the calls but guards
  them with `made` and `opened` flags.

Two pieces of the source are equivalents rather than transcriptions:

* The patch tests `ARCHIV` before it calls `archck`.  PL/M has no
  short-circuit `OR`, so the test moves to the top of `archck` (`if
  archiv = 0 then return 1`), and the call site becomes `if archck then`.
* To make room for the `[K]` test, the patch compares only the low byte
  of `NCOPIED` with zero, so a copy that matched a multiple of 256 files
  ends in FILE NOT FOUND.  The default build keeps the whole word, as
  V2.0 did; `--dri-exact` builds DRI's test (see
  [Building either release](#building-either-release)).

DRI's own CP/M 3 `PIP.PLM`, by the same author a few months later, has
the same changes in source form: `IF OBJ` in both places, `if not archiv
then return 1` at the top of `archck`, both extent numbers cleared, and
`if not kilds then call crlf`.  The version strings still say 2.0 in
DRI's V2.1 binary, and are left that way.

To check the reconstruction, four PIPs were run through the same 23
commands on the source-built V2.1 system: the rebuilt V2.1, DRI's V2.1
(on the disk as `XPIP.PRL`), DRI's V2.0, and the rebuilt V2.0.  The
commands covered `[A]`, `[O]`, `[K]`, `[E]`, `[N]`, `[V]`, `[G]`, `[T]`,
`[U]`, `[L]`, `[F]`, concatenation, multi-file copies and two errors.
The rebuilt V2.1 matched DRI's V2.1 on every console line and in all 26
output files, and the rebuilt V2.0 matched DRI's V2.0.  The two releases
differed only where the changes above say they should:

| Command | DRI V2.0 | DRI V2.1, and the rebuilt V2.1 |
|---------|----------|--------------------------------|
| `PIP C:OUTA.DAT=B:BIN.DAT[A]` (1024 bytes, ctl-Z at byte 300) | 384 bytes | 1024, same as the source |
| `PIP C:OUTOZ.DAT=B:BIN.DAT[OZ]` | 384 bytes | 1024 |
| `PIP C:OUTZ.COM=B:BIN.COM[Z]` | 1024 bytes | 384 |
| `PIP C:=B:BIG.DAT[A]`, then `PIP D:=B:BIG.*` (20K) | 4096 bytes, starting at record 128 | 20480, same as the source |
| `PIP D:=B:*.TXT[K]` | ends with an empty line | no empty line |
| `PIP C:NEW.TXT=B:NOSUCH.TXT` | FILE NOT FOUND; nothing left on C: | FILE NOT FOUND; `C:NEW.$$$` left, 0 records |

### GENSYS - a patch, not a recompile (GENSYS.PLM, LDRLWR.ASM)

`GENSYS.COM` is 8704 bytes in V2.0 and 9472 in V2.1, which made it look
like a recompile.  It is not.  V2.1 is V2.0's binary, patched in memory
and saved as 37 pages (2500H bytes).  Within V2.0's 8704 bytes the two
files differ in exactly 47:

* 2: the serial number.
* 1: the banner, `V2.0` to `V2.1` (0768H).
* 29: the high byte of every operand that addresses `.MEMORY`, GENSYS's
  sector buffer, which is `sctbfr` in `LDRLWR.ASM`.  It moves one page,
  from 253DH to 263DH.  A whole page keeps `low(sctbfr)` unchanged, and
  LDRLWR uses that low byte as an immediate.
* 15: five three-byte instructions, each turned into a `CALL` or `JMP`
  into the patch.

The patch occupies 145 bytes of the page `.MEMORY` gave up, 253DH-25CDH,
and every byte of it is accounted for.  Addresses here are load
addresses in DRI's V2.1 binary, 100H above the file offset.  The V2.0
routines were located from the source: `get$response` 0CC9H,
`setup$system$dat` 1909H, `LdRl` 1CBEH, `FCBin` 2035H, `system$data`
21BEH.

* **1961H**: `LXI B,<'Add system call user stacks '>` becomes `CALL
  255EH`.  That routine prints `Enable Compatibility Attributes `, calls
  `get$response(.system$data(96))` and `crlf`, then does the displaced
  `LXI`.  This is the new question.  It is asked after Breakpoint RST and
  defaults to N, because byte 96 of the unchanged default table is zero.
  The answer is 0FFH or 00H, and it is the byte `cliattr` in the XDOS
  patch area tests.
* **1991H, 19C7H**: `ADI 41H`/`DCR A` (`'A'+drive-1`) becomes a call to
  `CPI 0`/`JNZ`/`ADI 10H`/`ADI 40H`.  A drive is stored masked with 0FH,
  so P: is 0; V2.0 displayed it as `(@:)` and V2.1 displays `(P:)`.
* **1A56H**, after `get$param('Number of user memory segments')`: if the
  answer is 8 or more, it is set to 7, `*** Error Maximum Exceeded - 7
  Assumed ***` is printed, and the question is asked again with 7 as the
  default.  The memory segment table at system data 16-47 has eight
  entries and the first is MP/M's own.  V2.0 accepted any number and
  wrote the extra entries over the breakpoint vectors at 48.  The
  Implementor's Guide gives the range as 1 to 7.
* **1D4DH**, `ExitLdRl` in LDRLWR: `LXI H,0` becomes a call that first
  closes `FCBin`.  V2.0 left every SPR, RSP and BRS file it had loaded
  open; under MP/M each one holds a lock list entry and counts against
  the open file limit until it is closed.

DRI's addendum, "MP/M II Release 2.1 Compatibility Attributes", gives the
question as `Enable Compatibility Attributes (N) ?`.

The source carries these changes behind `$if MPM21` in
`src/overrides/MPMLDR/GENSYS.PLM`, and behind `IFDEF MPM21` in the
`LDRLWR.ASM` override for the close.  With `MPM21` undefined, the
`GENSYS.PLM` override compiles to exactly the same code as DRI's file.
The result cannot match DRI's byte for byte, so it was run under cpmemu
beside DRI's V2.1 `GENSYS.COM` with the same answers and SPR files, in
four sessions.  Together they cover the question answered Y and N,
defaults read back from a `SYSTEM.DAT`, drive P:, 7, 8 and 9 segments,
and an RSP/BRS pair.  In every session the dialogue was identical, the
BDOS close calls matched (9 closes where V2.0 makes 3), and `MPM.SYS`
and `SYSTEM.DAT` were identical apart from the six serial bytes.

Building this also exposed an older fault in the source build, not in
DRI's GENSYS: `LDRLWR.ASM`'s `mvi a,low(bitmap+128)`.  um80 before 0.3.49
assembled `low()` of a relocatable address as the low byte of its offset
within the module, with no relocation, so a source-built `GENSYS.COM`
read the next record of an SPR's relocation bit map at the wrong point:
its V2.0 `MPM.SYS` differed from the one DRI's GENSYS writes in 5872
bytes, and the V2.1 build read a fourth bit map record out of a file
that has three.  DRI assembled `LDRLWR.ASM` with ASM80, whose object
format carries the relocation.  With that fixed, the source-built V2.0
GENSYS writes the same `MPM.SYS` and `SYSTEM.DAT` as DRI's, but for the
serial number.  DRI's own `GENSYS.COM` relocates correctly; see
[ldrlwr_bug.md](ldrlwr_bug.md).

The system itself is generated by `tools/gensys.py`, not by
`GENSYS.COM`, so the two changes that reach `MPM.SYS` are made there as
well:

* `compatibility_attributes` (default false) writes byte 96.
  `scripts/gensys.sh` and `build_all.sh` take
  `--compat-attributes[=yes|no]`.  gensys.py tells the releases apart by
  XDOS's version word, `mpmver` at offset 61H of the code: 0120H or
  0121H.  A V2.0 system always gets zero at byte 96, for three reasons:
  * V2.0 GENSYS never asks for it, and has zero there in its default
    table.
  * No V2.0 binary reads it: V2.1's `cliattr` is the only code in either
    release that addresses system data offset 60H.
  * DRI's own V2.0 `NUCLEUS/MPM.SYS` happens to have 0FFH there,
    presumably copied through from an old `SYSTEM.DAT`, and a V2.0 XDOS
    ignores it.
* More than seven user segments is reduced to seven, with DRI's message,
  for both releases.

In a booted V2.1 system generated with the attributes on, a program that
prints byte 1DH of its own process descriptor reports:

| SET on the program file | byte 1DH |
|---|---|
| none | 00H |
| `[F1=ON]` | 80H |
| `[F4=ON]` | 70H |
| `[F1=ON,F3=ON]` | A0H |

F4' also sets F2' and F3', as DRI's addendum says.  With the attributes
off, all four report 00H.  The same holds for an `MPM.SYS` written by the
source-built V2.1 `GENSYS.COM`.

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
* Resident system processes were built wrong and left out of every
  generated system.  Both are fixed.
  * `SCHED.RSP`, `SPOOL.RSP` and `MPMSTAT.RSP` had been linked from two
    modules each (`*BRS.PLM` + `*RSP.PLM`), which put the BRS's header and
    a CP/M program entry where MP/M expects the process descriptor.  They
    are now built the way DRI's `UTIL2/*.SUB` build them: each `*.RSP`
    from `*RSP.PLM` alone - offset 0 the word MP/M sets to the BDOS entry,
    the descriptor at 2 and the queue at 2+52 - and the code as a
    separate `*.BRS` (a new build output) from `*BRS.PLM` and
    `src/brs_runtime.mac`, which stands in for `BRSPBI.ASM`.  A BRS's
    offset 0 is OS, offset 2 the stack pointer and offset 4 the name.
  * Against DRI's files, `ABORT.RSP` is byte for byte identical.  The
    other RSPs match in every byte DRI's DATA and INITIAL lists define.
    The BRS headers have the same shape, and each one's stack-pointer word
    addresses the process entry with 19 `0C7C7H` below it.
  * `scripts/gensys.sh` loads ABORT, MPMSTAT, SCHED and SPOOL (with their
    `.BRS` files) ahead of SFTP, into every system.  Making room for them
    in common memory meant shrinking the XIOS's checksum vectors, which
    are never used (CKS is 0).
  * The spooler also needed the XIOS to move disk records in the calling
    process's bank (through SWTUSER and SWTSYS) rather than in the last
    user bank selected.  The same fault had broken every SFTP and HTTP
    file read.
  * Building them from DRI's sources found four uplm80 defects - a
    STRUCTURE's DATA and INITIAL lists, a string in one, a LITERALLY list
    in one, and a RETURN inside a counted DO loop.  Putting the spooler
    RSP in the system then showed `SPOOL.PRL` writing its message to the
    spooler over the queue's own pointer: uplm80 compiled
    `AT (.tbuff-1)` as the location counter, and um80 kept only the last
    of two constants added to an external.  They are fixed in uplm80
    0.3.7 and um80 0.3.49.
* SUBMIT and SPOOL (when there is no SPOOL RSP and `SPOOL.PRL` prints
  the files itself) build their buffers from their last variable up to
  the top of the memory segment - `rbuff` at `minimum$buffer`, `buffer` at
  `dummy$buffer`.  That relies on Intel's LOCATE, which put the stack
  below the data.  uplm80 puts its string constants, the procedures'
  shared locals (`??AUTO`) and the stack after the last variable, so a
  command file over 1K came out garbled ("Bad entry"), and the spooler
  lost its buffer pointer, which is one of those locals, a few records
  in.  Both overrides now put the buffer at `.MEMORY`, and
  `tools/build.py` asks MP/M for the minimum the sources had reserved in
  the image (400H and 80H, in the `.PRL` header).
* ul80 0.3.48 did not relocate `__END__` in a `.PRL`: a reference to it
  was not marked in the bit map, so `.MEMORY` was right only when the
  program was loaded at a segment base of 0000H.  PIP, ED, SDIR, STAT,
  SUBMIT and SPOOL use it.  Every memory segment `gensys.sh` generates
  starts at 0000H (seven banks of 0000-BFFFH), so nothing here showed it;
  a system with a segment based elsewhere would have.  ul80 0.3.49 marks
  those references, and a source build now needs um80_and_friends 0.3.50
  or later.
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
