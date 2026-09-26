# Changelog

Notable changes to mpm2, a Z80 MP/M II emulator with SSH terminal access.

This file begins at 0.3.5. Releases 0.3.0 through 0.3.4 were tagged without
release notes; they are summarised below from their commits, in less detail
than they would have carried at the time, so the record before 0.3.5 is short
rather than empty.

## [Unreleased]

### Added

`tools/verify_dri.py` compares five more files with DRI's: `ABORT.RSP`,
`DUMP.PRL`, `GENHEX.COM` and `GENMOD.COM` in both releases, and
`BNKBDOS.SPR` in V2.1, the release whose source DRI shipped (see Known issues
under 0.3.6). All but `GENHEX.COM` are identical, and `GENHEX.COM` is but for
70 bytes no source sets (see Fixed). Every file is now compared whole, the
header page and the padding of the last record included, and a difference is
reported by the part it is in - header, image, bit map or padding - at its
offset in that part.

### Changed

`tools/v21/where.py` reads the sources of the checkout it is in, not a fixed
`/Users/wohl/src/mpm2`, which from another checkout mapped offsets against
that checkout's files; it and `syms.py` use the installed um80, not a copy
put on `sys.path` from `../um80_and_friends`. Their listings go to
`build/v21` in the checkout (`V21` still overrides it) instead of `/tmp/v21`,
so two checkouts do not assemble into each other's.

### Fixed

The files DRI made with LINK, RMAC's linker - XDOS, BNKXDOS, RESBDOS, TMP,
BNKBDOS, `ABORT.RSP` and `DUMP.PRL` - end their last record with ^Z (1AH)
after the relocation bit map, as DRI's do; ul80 fills it with zeros. So
`tools/build.py` pads the output of every target DRI linked (an `.SPR`,
`.RSP` or `.PRL` of assembler modules alone) with 1AH. DRI made its PL/M
`.PRL`, `.RSP` and `.BRS` files with GENMOD, which fills with zeros, and
those stay as they are. The padding is past the bit map, where no loader
reads. `verify_dri.py` used to stop at the end of an `.SPR`'s bit map
because "DRI's linker left stale bytes" there; they were the ^Z padding.

`run_tests.sh src` carried on when the source build failed ("Continuing with
partial build"), and tested whatever binaries and disk image an earlier build
had left, so a pass did not show that the build worked. A failed build now
fails the run, with the end of `build/mpm2_src_build.log`. And
`tools/build.py` no longer links a target one of whose sources failed to
compile or assemble: it linked the rest, so a failed `MPMLDR.PLM` left an
`MPMLDR.COM` of `LDMONX` and the loader's BDOS alone in `bin/src`.

GENHEX, GENMOD and the part of `MPMLDR.COM` that is DRI's assembler source are
put together as DRI's submit files did it, with MAC and LOAD, and a byte no
statement loads - a DS area, the gap before a later ORG - is now what LOAD
wrote there, not zero. LOAD (`UTIL3/LOAD.PLM`) keeps a 256-byte buffer,
stores each byte at the index of its address's low byte and writes the buffer
out a record at a time, so a byte nothing loads is the last one stored at that
index, 256 bytes below it; `tools/genmod.py` has a copy of it, `load`.
`GENMOD.COM` was 102 bytes from DRI's, its variables and stack at
0584H-05EFH, and is now DRI's byte for byte. The 233 bytes of `MPMLDR.COM`
that `verify_dri.py` let through as "no source sets" - LDRBDOS's DS areas and
the gap up to LDRBIOS - are DRI's too, so all of 0D00H-177FH is. `GENHEX.COM`
is DRI's but for 70 bytes, which the build cannot know: 64 are its stack, in
the first 256 bytes of the program, where LOAD writes what its buffer held
when it started - in DRI's file part of MAC.COM, but not where the LOAD DRI
shipped would have found it - and 6 are variables before its `patch:`
routine, which are zero in DRI's file, as if it had been LOADed before
`patch:` was added. The source release's own rebuild of it,
`mpm2src/UTIL3/GENHEX.COM`, is the build's byte for byte but for the stack.

## [0.3.6] - 2026-09-26

MP/M II V2.1 now builds from source as well as V2.0. In both releases XDOS,
BNKXDOS, RESBDOS and TMP come back byte for byte identical to Digital
Research's, and a source-built system runs every utility tried the way DRI's
own binaries do.
Every generated system now carries DRI's four resident system processes, and
on the way the emulator's disk transfers, its XIOS results and SFTP and HTTP
file access were put right.

This release needs uplm80 0.4.0 or later, upeepz80 0.2.6 or later,
um80_and_friends 0.3.51 or later and cpmemu 4.10.0. uplm80 and upeepz80
compile the SFTP resident system process in every build, as well as the
utilities of a `--tree=src` build; um80 and ul80 assemble and link the XIOS,
the loader BIOS and the whole of a `--tree=src` build; cpmemu provides the
emulator's Z80 (`libqkz80`) and `util/cpm_disk.py`, which writes the disk
image. What the older releases get wrong here:

- uplm80 before 0.4.0 passes a procedure's arguments on the stack, or
  writes them into the procedure's own storage, where PL/M-80 passes the last
  in DE and the one before it in BC. What it compiles does not work with
  DRI's `X0100.ASM`, `BRSPBI.ASM` and `LDMONX.ASM`, which the source build
  now links as DRI did (see Changed), or with the SFTP RSP's glue, which
  takes its arguments in registers too. uplm80 0.4.0 refuses an upeepz80
  before 0.2.6, which made `push ... / call p / ret` into `jp p`, after
  which p took its return address for its first argument.
- uplm80 before 0.3.7 computes `x MOD 0` as 0 where PL/M-80 gives `x`, so
  every source-built SDIR reprinted its heading before each line of output
  (SDIR's default page length is 0). It also laid out the initial values of
  the resident system processes' descriptors and queues wrongly, took only
  the first word of a `LITERALLY` list in an `INITIAL` list, returned from
  inside a counted `DO` loop into the loop's count, compiled
  `AT (.external - 1)` as the location counter, and put its stack and shared
  locals after the last variable, which SUBMIT and SPOOL use as the start of a
  buffer. The source build now compiles all of that code as DRI wrote it.
- um80 before 0.3.51 has no `--dri`, with which the source build reads every
  one of DRI's assembler sources as MAC and RMAC read it, and ul80 before
  0.3.51 no `--fatal-mult-def`, which every link `tools/build.py` makes
  passes, and the BNKXIOS and SFTP links too (see Changed). um80 before
  0.3.50 cannot cut PUBLIC and EXTRN names to six characters (`-t`), as RMAC
  does; the nucleus does not link without it. Before
  0.3.49 it assembled `LOW()` of a relocatable address as an absolute byte,
  read a forward `EQU` of a later label as 0, and kept only the last of two
  constants added to an external; ul80 before 0.3.49 left references to
  `__END__`, PL/M's `.MEMORY`, out of a `.PRL`'s relocation bit map.
- upeepz80 before 0.2.5 made rewrites that changed a register or a flag that
  was read afterwards.
- cpmemu's `cpm_disk.py` before 4.10.0 fails with `UnicodeDecodeError` on
  `add` and `delete` when a file on the disk has an F1'-F4' attribute set,
  which `--compat-attributes` now puts to use.

### Added

MP/M II V2.1 can be built from the same tree as V2.0. DRI published sources
for V2.0 only, so the V2.1 changes were recovered from the two masters in the
archive — `mpm2src/CONTROL`, V2.0, and `mpm2dist`, V2.1 — and put into
`src/overrides` behind `IFDEF MPM21` / `$if MPM21`. `docs/mpm2_v21.md`
describes every change and the evidence for it.

- `./scripts/build_all.sh --tree=src --version=2.1` builds the V2.1 system,
  which boots and reports `MP/M II V2.1 / Copyright (C) 1982, Digital
  Research`. `--version=2.0`, the default, builds V2.0, which the V2.1
  changes leave as it was.
- `tools/build.py` gained `--version`, `--serial`, `--dri-exact` and
  `--output-dir`, and `build_all.sh` passes the first three through.
  `--serial dri` builds in the serial number from DRI's master instead of the
  sources' `654321`. `--dri-exact` also leaves out the local fixes the
  overrides keep behind `DRIEXACT` — `TMPSUB.ASM`'s stack pointer save, which
  DRI had commented out, and the whole-word `NCOPIED` test in V2.1's PIP — and
  fills the bytes of ASM, RDT and DDT that no source sets the way DRI's GENMOD
  found them.
- `tools/verify_dri.py` builds both releases with `--dri-exact` and compares
  them with DRI's binaries. XDOS.SPR, BNKXDOS.SPR, RESBDOS.SPR and TMP.SPR
  come back byte for byte identical in both releases, and so do RDT.PRL and
  DDT.COM; ASM.PRL does but for 11 bytes no source sets (see Fixed), and the
  part of MPMLDR.COM that DRI assembled with MAC but for 233 (see Changed).
- The nucleus changes, one line each: the MX queues a process owns are
  released on its way out through XDOS rather than in the dispatcher;
  `pd(1dh)` carries the F1'-F4' attributes of the command's file; a process's
  abort return address is planted relative to its bank's system call user
  stack; the list number is masked to its low nibble; the XIOS may print
  through `extjmptbl` only for the process that owns the console; a
  pushed-back keyboard character counts as console-ready; delete, rename and
  set file attributes copy the FCB back out; a shell error keeps the code
  already in HL; a submit file already started stays in force; and any
  non-zero return from read random is a disk error.
- The transients. None of them can match DRI's binary byte for byte, since
  uplm80 is not PL/M-80, so each was checked against DRI's patch instruction
  by instruction, and the larger ones also by running them beside DRI's
  binary on the same system:
  - MPMLDR: the banner.
  - SHOW: `show users` lists user 15. V2.0's array had fifteen elements for
    sixteen user numbers.
  - PRINTER: a list number is valid up to the number of printers the system
    was generated with, not a fixed sixteen.
  - SCHED.RSP: the scheduler's process name marks it a system process, as
    Spool's, MPMSTAT's and Abort's already did.
  - SPOOL.PRL: the spooler drops to priority 201 before it lists; it checks
    the files exist with F5' set, as it lists them; `spool a.txt[d]` no
    longer stops with `Can't Open File = D`; and the message it prints on
    detaching loses its last two lines.
  - SPOOL.BRS: the resident spooler detaches from the console before it
    waits for the next request.
  - SDIR: V2.0's check for room in its file table could never fail, so on a
    drive with more matching files than fit in its memory segment SDIR wrote
    over the jump to the XDOS at the top of the segment. DRI's own V2.0 SDIR
    lists 115 files in a small segment and hangs the whole system on 116.
    V2.1 measures the room from the last record it stored, prints "Out of
    Memory" and lists what fitted, and asks MP/M for 4K more in the `.PRL`
    header, room for about 180 more files. `tools/build.py` gives only the
    V2.1 build the 4K.
  - PIP, five changes. `[A]` no longer forces a character copy, which cut a
    binary file off at its first ctl-Z. `[O]` takes effect in file-to-file
    and multi-file copies, and in return a `.COM` file copied file-to-file
    with a character option and no `[O]` stops at ctl-Z, as DRI's V2.1 does.
    A multi-file copy without `[A]` copies a file whose extents are all
    marked archived from its beginning, where V2.0 started part way in. `[K]`
    also drops the final new line of a multi-file copy. And an error no
    longer closes and deletes the destination's `.$$$` file, so a copy that
    fails after the destination was made leaves it behind, as DRI's V2.1
    does. On 23 commands covering `[A] [O] [K] [E] [N] [V] [G] [T] [U] [L]
    [F]`, concatenation, multi-file copies and errors, the rebuilt V2.1 PIP
    matches DRI's V2.1 on every console line and in all 26 output files, and
    the rebuilt V2.0 matches DRI's V2.0 the same way.
  - GENSYS: V2.1's GENSYS.COM is not a recompile but V2.0's with a 145-byte
    patch where its sector buffer used to start. It asks "Enable
    Compatibility Attributes (N) ?" (system data byte 96), shows drive P: as
    `(P:)` instead of `(@:)`, limits the user memory segments to seven with
    "*** Error Maximum Exceeded - 7 Assumed ***", closes each SPR, RSP and BRS
    file after loading it, and prints the V2.1 banner. Run under cpmemu with
    the same answers, the rebuilt one gives exactly the dialogue of DRI's
    V2.1 GENSYS.COM, and an MPM.SYS and SYSTEM.DAT identical to its own but
    for the serial number.

`--compat-attributes[=yes|no]` for `scripts/build_all.sh` and
`scripts/gensys.sh`, and a `compatibility_attributes` key in
`tools/gensys.py`'s configuration, answer V2.1 GENSYS's new question. The
default is no, as in DRI's GENSYS. With yes, a V2.1 system copies a program
file's F1'-F4' attributes into byte 1DH of its process descriptor, as DRI's
Release 2.1 addendum describes; until now every generated system left byte 96
zero, so the feature never fired. In a booted V2.1 system, a program marked
with SET `[F1=ON]`, `[F4=ON]` or `[F1=ON,F3=ON]` now sees 80H, 70H or A0H
there. A V2.0 XDOS never reads byte 96, so `gensys.py` leaves it zero for a
V2.0 system and says so. `build_all.sh` rejects a value other than yes or no
before it starts, instead of failing at the GENSYS step once everything else
is built.

Every generated system now has DRI's four resident system processes — ABORT,
MPMSTAT, SCHED and SPOOL — next to the emulator's SFTP RSP, taken from the
selected tree. Before, `sched 12/31/99 23:59 dir` answered "Resident portion of
scheduler is not in memory", and `abort` and `mpmstat` ran as transients.
`tools/build.py` builds a resident system process the way DRI's `UTIL2/*.SUB`
files do: the `.RSP` from `xxRSP.PLM` alone, and the banked code as a new
`.BRS` output type, from `xxBRS.PLM` and DRI's `BRSPBI.ASM`, with
`src/brs_runtime.mac` for the compiler's own BDOS entry. It used to link the
two into one `.RSP`, with the BRS's header and a CP/M program entry where
MP/M expects the process descriptor.
`ABORT.RSP`'s header, image and relocation bits are now DRI's, and the other
three `.RSP` files match every byte DRI's declarations define.
`tools/gensys.py` follows DRI's GENSYS in two more ways: it loads a `.BRS`
exactly when the RSP's process descriptor is in memory segment 0, and it
refuses an RSP that would extend below the common base.

`run_tests.sh` has three new tests, all part of `all` and `src`:

- `rsp` (`scripts/test_rsp.exp`): MPMSTAT lists the RSPs' queues, SCHED runs
  a command that is due, ABORT answers through its queue, the spooler lists a
  file and deletes it, and STOPSPLR stops it. Each check waits for what it
  checks rather than for a fixed time, and cannot pass by accident: "Abort
  failed." counts only after the CLI's "Msg Qued", since ABORT.PRL prints the
  same words; a spooler check fails if PIP did not make the file it spools;
  and STOPSPLR is taken to have stopped the spooler only once a marker file
  queued behind the stopped one is gone.
- `http`: a file read over HTTP, through the SFTP RSP.
- `sftp` (`scripts/test_sftp.exp`): files put over SFTP read back over SFTP
  and HTTP and are then used from a console — `type` one, `submit` the other;
  PIP copies a file HTTP is reading; HTTP is refused a file ED has open and
  the system carries on; and a 64K file goes up and comes back intact while
  HTTP reads another file, which stays intact too. The transfer is given 180
  seconds, since it runs through the Z80 RSP a record at a time: it takes
  about 5 seconds on an idle host, and failed on every tree alike, DRI's
  included, with the host's load average at 80-100.

`tools/v21/` keeps the tools the V2.1 reconstruction was done with. `annot.py`
diffs two `.SPR` or `.PRL` images and annotates each differing run with the
source lines it covers, `where.py` maps an offset in a linked image back to
its module and source line, and `spr.py`, `syms.py`, `disasm.py` and
`rawdiff.py` each do one part of that. None of them is part of the build.

### Changed

`--tree=src` produces a running system. A fully source-built MP/M II V2.0 boots,
loads and runs transient programs, and every PL/M utility tried — `dir`, `sdir`,
`stat`, `tod`, `user`, `console`, `show`, `type`, `dump`, `set`, `prlcom`,
`printer`, `stopsplr`, `sched`, `spool`, `abort`, `ren`, `submit`, `mpmstat`,
with and without arguments — gives the same output as DRI's own binaries —
`stat` prints `A: RW, Space:     7,512k`, `dir` lists `A: $3$      SUP`, `tod`
prints `Mon 09/14/81 00:00:19`, `user 0` prints `User Number = 0`, `console`
prints `Console = 3`. Before this release a source-built system printed a
program's load line and then dropped the session, whatever the program was.

More of the source build is DRI's own text. SUBMIT (`UTIL5/SUB.PLM`), the
banked halves of the scheduler and MPMSTAT (`UTIL2/SCBRS.PLM`, `MSBRS.PLM`) and
the resident halves of the spooler and MPMSTAT (`UTIL2/SPRSP.PLM`, `MSRSP.PLM`)
build from DRI's files as they stand, and so do BNKBDOS, `NUCLEUS/MEMMGR.ASM`,
the loader's BDOS (see below) and `MPMLDR/LDMONX.ASM`, the loader's BDOS
interface (see the calling convention, below). The overrides that are left
carry the V2.1 reconstruction, the serial number and a few local fixes:

- The ones that carry the V2.1 reconstruction are DRI's text apart from their
  V2.1 changes: `SPBRS.PLM` has DRI's `restarts` stack and its `DO` loop,
  `SCRSP.PLM` DRI's `DATA` and `INITIAL` lists, `UTIL5/MSPL.PLM` its message
  at `.tbuff-1` and its buffer at `.dummy$buffer`, `MPMLDR/LDRLWR.ASM` its
  `low()` immediates, and `UTIL7/DSE.PLM` the two bytes of `hash$table`'s
  declaration that DRI's file has with the parity bit set, which uplm80
  clears as it reads a source, as PL/M-80 did. `MPMLDR.PLM` also keeps its
  disabled serial check, with DRI's loop under the early return.
- `tools/build.py` assembles DRI's `.ASM` sources with six-character PUBLIC and
  EXTRN names (um80 `-t`), as RMAC wrote them into the object file. DRI's
  nucleus depends on that: `DSPTCH.ASM` refers to `DATAPG.ASM`'s `memseg` as
  `memsegtbl`, for one. The five aliases the nucleus overrides carried instead
  are gone, and the V2.1 patch area enters CLI at `prbrls`, which does not
  collide with CLBDOS's `printb` at six characters.
- A `$` inside a name or a binary constant is DRI's spelling again —
  `BNKXDOS.ASM`'s `pw$fld`, `CLI.ASM`'s `0001$1111b` — and that includes the
  28 names DRI's text spells two ways, which RMAC takes for one name since it
  ignores the `$`: `MPM.ASM` stores to `nmb$lst`, which `DATAPG.ASM` defines
  as `nmblst`, `CLI.ASM` calls `open$test` and defines `opentest`, and 23 of
  them are in `BNKBDOS.ASM`. um80 0.3.51 ignores the `$` too, with `--dri`
  (see below), so the override of `BNKBDOS.ASM`, which spelled each of its 23
  one way, is gone, and so is `MEMMGR.ASM`'s, which wrote six line ends of CR
  and 8AH, a line feed with the parity bit set, as CR LF: um80 0.3.51 clears
  bit 7 of a source byte. `CONBDOS.ASM`'s `patch$size` has its `$` back too.
  `NUCLEUS/BNKBDOS1.ASM` and `BDOS30.ASM`, `$`-stripped copies that nothing
  built, and a copy of `MPMLDR/LDRBDOS.ASM` identical to DRI's are gone.
- XDOS, BNKXDOS, RESBDOS and TMP are still identical to DRI's in both
  releases, and BNKBDOS to DRI's V2.1. SUBMIT, SPOOL with and without the
  spooler RSP, SCHED and MPMSTAT give the same results as DRI's binaries on
  V2.0 and V2.1. SUBMIT.PRL and SPOOL.PRL ask MP/M for no extra memory, as
  DRI's do: each program's buffer starts at one of its last variables and runs
  on to the top of its memory segment.

`tools/build.py` assembles every one of DRI's assembler sources with um80
0.3.51's `--dri`, which reads it as MAC and RMAC do where they differ from
MACRO-80: a `$` inside a name is ignored, the first word of a statement is a
label without a colon when it is no instruction or directive
(`UTIL3/GENHEX.ASM`'s `OBP DS 1`), `PUSH A` is `PUSH PSW` (`RESBDOS1.ASM`,
`BNKBDOS.ASM`), a line that starts with `*` is a comment, and a `!` ends a
`;` comment and starts the next statement, as it does outside one. Without it
um80 0.3.51 reads a source as MACRO-80 does, and GENHEX, RESBDOS and BNKBDOS
do not assemble. The `.mac` modules in `src/`, what uplm80 writes and the
emulator's own `asm/*.asm` are MACRO-80 text and are read that way. Two of
the V2.1 overrides noted the code they replace as DRI wrote it, several
statements to a line - `;<TAB>pop h! lxi h,0007! jmp shell$err` in
`RESBDOS1.ASM`, `;<TAB>push d! call constf! pop d` in `CONBDOS.ASM` - which
MAC, and so um80 0.3.51, assembles after the first `!`; the notes now have a
statement to a line, and V2.1's RESBDOS.SPR is DRI's again. `tools/v21/where.py`
and `syms.py` pass `--dri` too, so `PRISTINE=1` works for every target.
With all of that, every target but GENSYS.COM and MPMLDR.COM (below) is byte
for byte what the same tree built with um80/ul80 0.3.50 and the overrides
this release removes, in V2.0 and V2.1, with and without `--dri-exact`.

The loader's BDOS is assembled from DRI's `MPMLDR/LDRBDOS.ASM` instead of
being copied out of DRI's `MPMLDR.COM`; um80 0.3.50 could not assemble it as
MAC does. `MPMLDR.COM` is put together the way `MPMLDR.SUB` did it: the PL/M
loader at 0100H, `LDRBDOS.ASM` at its ORG 0D00H and `LDRBIOS.ASM`, DRI's
skeleton loader BIOS, at 1700H, each assembled with `--dri --aseg`, to the
end of the 128-byte record LOAD wrote last. Every byte a statement loads is
DRI's, in V2.0 and V2.1. The 233 bytes none loads — LDRBDOS's DS areas at
0E8CH-0EBDH and 0EC0H-0EC3H, and 164DH-16FFH, its variables and the gap up to
LDRBIOS — hold whatever was in memory in DRI's file, and are zero here; 226 of
them differ. `tools/verify_dri.py` compares 0D00H-177FH of `MPMLDR.COM` with
DRI's and reports those 233 as bytes no source sets. At boot the emulator's
own LDRBIOS is loaded over the skeleton, as before.

Every link `tools/build.py` makes, and the emulator's BNKXIOS, SFTP.RSP and
SFTP.BRS links, pass ul80's `--fatal-mult-def`: a global two modules define
stops the link. LINK-80, and ul80 without the flag, warn, use the first
definition and write the program, which is how an exported `PRINTBrlsfile`
in CLI, six characters of which are `PRINTB`, once took CLI's and ATTACH's
calls to CLBDOS's `printb` (see above). GENSYS was the one link with such a
name: `X0100.ASM` and the CP/M runtime, `src/cpm_runtime.mac`, both defined
BDISK, BOOT, BUFF, FCB, FCB16, MAXB, MON1, MON2, MON2A and TBUFF, and the
first, X0100's, won. GENSYS now links what DRI's `GENSYS.SUB` linked,
GENSYS, LDRLWR and X0100, and nothing else. Run under cpmemu with every
question answered by default, beside DRI's GENSYS.COM with the same SPR and
RSP files, the rebuilt one prints DRI's dialogue and writes DRI's MPM.SYS and
SYSTEM.DAT but for the six bytes of the serial number, in V2.0 and V2.1.

The PL/M programs link with DRI's own interface modules, unmodified, as DRI's
submit files linked them. uplm80 0.4.0 passes a procedure's arguments the
way Intel's PL/M-80 does - the last in DE, the one before it in BC, any
others pushed and taken off by the procedure - so a `MON1 (f, a)` it cannot
open-code arrives with the function in C and the parameter in DE, which is
what the BDOS wants. Every `.PRL` now links `PLM_WORK/X0100.ASM`, whose MON1,
MON2, MON2A and MON3 are `equ 0005h` and whose FCB, TBUFF and the rest are
the page-zero addresses, from a module of their own so that the relocation
bit map gets them; every `.BRS` links `UTIL2/BRSPBI.ASM`, which jumps to the
BDOS through the `.RSP`; and `MPMLDR.COM` links DRI's `MPMLDR/LDMONX.ASM`,
whose LDMON1 and LDMON2 are the loader BDOS at 0D06H. What stood in for them,
for uplm80's old stack convention, is gone: `src/cpm_runtime.mac`,
`src/mpm_runtime.mac`, the MON1 to MON2A of `src/brs_runtime.mac` and the
override of `LDMONX.ASM`. `src/mpm_pagezero.mac` keeps only the compiler's
own `??BDOS`, `??BOOT` and `??MAXB`, and `src/brs_runtime.mac` only `??BDOS`
and `??BOOT`. A `.PRL` is 12 bytes shorter than it was, a `.BRS` 1 and the
PL/M loader in `MPMLDR.COM` 33. `tools/build.py` reads X0100 and BRSPBI as
it reads DRI's other assembler sources (`--dri -t`) and assembles the
runtime modules into `build/src/runtime/`, since GENSYS assembles
`MPMLDR/X0100.ASM`, which is not PLM_WORK's, into `build/src`. The SFTP RSP's
glue, `asm/sftp_glue.asm`, takes its arguments in BC and DE, as
`sftp_brs.plm` now passes them, and loses `COPYFCBNAME`, which nothing
called.

`tools/genmod.py` no longer refuses an ORG in column 1, which um80 0.3.51
assembles as an ORG, as MAC does.

CI and the release workflow check the toolchain out at its release tags —
um80_and_friends v0.3.51, uplm80 v0.4.0 and cpmemu v4.10.0 — and install
upeepz80 0.2.6 or later, instead of each tool's main branch, so a change to
the toolchain cannot break an mpm2 build that nothing in mpm2 has changed.

`scripts/gensys.sh` generated the system in a fixed `/tmp/gensys_work`, which
it removes first, and `run_tests.sh` wrote the emulator's log and the source
build's to fixed names in `/tmp`, so two checkouts building or testing at once
cleared each other's GENSYS inputs and interleaved their logs. The work
directory is now `build/gensys_work`, which `GENSYS_WORK` overrides, and the
logs are `build/mpm2_test.log` and `build/mpm2_src_build.log`.

The XIOS's checksum vectors shrink from 256 bytes per drive to 16. They are
never used — CKS is 0 in the one DPB the drives use, since a fixed disk is
not checked for a media change — and the room they took in common memory is
what DRI's four RSPs need: with the RSPs in, GENSYS stopped with "XIOS common
base BD4BH below configured common base C000H".

`bin/src` is rebuilt with uplm80 0.4.0, um80 and ul80 0.3.51 and upeepz80
0.2.6: V2.0, `build_src.sh`'s default. It was last rebuilt before any of this
release's other changes and now has them all: the resident system processes
as DRI's `.RSP` and `.BRS` pairs (`MPMSTAT.BRS`, `SCHED.BRS` and `SPOOL.BRS`
are new), `ABORT.RSP` and `DUMP.PRL` as DRI linked them, `ASM.PRL`,
`RDT.PRL` and `DDT.COM` made with GENMOD, `MPMLDR.COM` with the loader's
BDOS assembled from DRI's source, and every PL/M program as uplm80 0.4.0
compiles it, linked with DRI's interface modules. XDOS, BNKXDOS, RESBDOS,
TMP, BNKBDOS, GENHEX and GENMOD are the same as before.

### Fixed

Every PL/M utility now prints what DRI's own binary prints. The remaining
defects were all in the compiler, and were found by putting DRI's binaries on
the same disk under `X`-prefixed names and comparing the two outputs command by
command — 24 comparisons, all identical:

- A `PUBLIC` procedure took its arguments the way a private one does, which a
  caller in another module cannot do. SDIR's `pdecimal(v, prec, zerosup)` read
  two of its three arguments from slots nobody had written.
- `AT(.MEMORY)` was a label at the end of the module rather than the linker's
  `__END__`, so SDIR's hash table landed in the middle of the program and
  cleared another module's strings.
- A variable `BY` step in `DO I = A TO B BY I` was treated as `BY 1`, so SDIR
  counted every allocated block twice on a large disk.
- A `BYTE` loop index was not widened before the 16-bit loop path, so
  `do i = 0 to last(user)` never terminated and `show users:` printed until the
  session died.
- `AT(...)` understood only a bare `NAME(<literal>)`; a constant expression or a
  structure designator became `EQU $`, the assembler's location counter. STAT
  read a stray byte as its `$` parameter, so `stat <file>` set the file
  read-only instead of listing it; PIP and PRLCOM were miscompiled the same way.
- A declared variable did not shadow the `ZERO` condition-flag built-in, so
  STAT's zero-suppression flag read the Z flag and printed `(00001 file,
  00001-1k blocks)`.
- `x BASED s.m` read its pointer from the start of `s` rather than from the
  member, so SDIR matched its command line against address 0 and answered
  "File Not Found." to every argument.
- `DECLARE x (*) BYTE DATA (...)` never took its extent from the data, so
  `LAST(x)` was -2. PIP's delimiter table is declared that way, so PIP
  recognised no delimiter — not even the `=` between destination and source —
  and answered "INVALID FORMAT" to every command.
- A nested procedure's return type was not known where it is used, so a `BYTE`
  result was read out of `L` instead of `A`.
- MP/M II's assembler, DDT, GENHEX and GENMOD are MAC sources: absolute code,
  each module carrying its own `ORG`, which DRI assembled separately and
  concatenated as HEX. Assembled as relocatable they were stacked one after
  another, so `ASM.PRL` came out at 36971 bytes against DRI's 8171 and began
  with zeros where its entry should be. um80's new `--aseg` assembles them the
  way MAC does, and they no longer link against the PL/M runtime they never
  used. The assembler and the debugger then needed GENMOD as well; see
  below.

`stat` printed its drive line 1837 times and never printed a figure, because
um80 assembled a label named after a mnemonic as the opcode byte. UTIL4/STAT.PLM
declares `add: procedure(ap,bp)` for its BCD arithmetic, and `call add(...)`
came out as `CALL 0080H` — 80H is `ADD A,B`, and 0080H is the DMA buffer. STAT
executed its own command tail as instructions, fell into its entry jump at 0100H
and restarted. Fixed in um80; a symbol now wins over a mnemonic of the same
name, and `DB MOV` still gives the opcode byte where nothing defines MOV.

Five defects in what uplm80 emits for a declaration, all of which produced
programs that linked cleanly and then wrote through a null or short pointer:
the MP/M stack setup was four bytes where DRI's `.start-3` entry convention
needs three; a STRUCTURE initialiser was emitted at one width rather than each
member's, which left UTIL7/DM.PLM's ten-byte parser control block five bytes
long and its two pointers at zero; values the emitter could not place were
dropped silently rather than reported; `AT(.MEMORY)` was an EQU, which reads as
zero above its own declaration, so UTIL7/DSE.PLM's hash table cleared 128
entries over page zero; and the compiler's page-zero symbols shared a namespace
with PL/M identifiers, so UTIL7/DM.PLM's variable `bdos` captured the BDOS
entry. Fixed in uplm80 0.3.6.

A source-built nucleus could not run a transient program at all. `LXI
H,PDTBL-34H` in `NUCLEUS/CLI.ASM` assembled to `PDTBL+34H`, because um80 had the
two branches of an external-symbol expression swapped and added a constant it
should have subtracted. The CLI indexes the process descriptor table from there
and primes a new process's initial stack with the program's entry address, so
every entry it touched was 0x68 bytes past the real table and the dispatcher
resumed each transient at whatever lay beyond it: MP/M loaded the `.PRL`,
printed its load line, and warm-booted. Fixed in um80; the CLI's reference now
assembles to the same bytes as DRI's own `XDOS.SPR`.

The PL/M runtime took its arguments in the wrong place. `MON1`/`MON2`/`MON2A`/
`MON3` read the BDOS function from `C` and the parameter from `DE`, which is
DRI's PL/M-80 convention — it is why `PLM_WORK/X0100.ASM` can define all three
as `EQU 0005H` — but uplm80 before 0.4.0 passes arguments to an external
`PROCEDURE` on the stack. It open-codes `MON1` and `MON2` when the function
number is a constant, which is why this went unseen: the routines were only
reached when it could not, and then they ran on whatever `C` and `DE`
happened to hold. `DIR.PLM`'s
`parse` (XDOS 152, through `mon3`) was one such call, so DIR parsed its command
line from garbage. uplm80 0.4.0 passes the arguments in `C` and `DE`, as
PL/M-80 did, and the runtimes are gone: the programs link DRI's `X0100.ASM`,
whose `MON1` to `MON3` are the BDOS entry itself (see Changed). The BDOS
returns a byte in `A` and an address in `HL`, which is what uplm80 reads for
a `BYTE` and an `ADDRESS` result.

Assignments of a comparison lost their store, through a defect in upeepz80's
dead-store elimination — it treated the compiler's own `??` join label as a
procedure entry, and judged liveness only to the end of the enclosing
procedure. `DIR.PLM`'s `incl$sys = (fcb16(1) = 'S')` and `STAT.PLM`'s
`sys = ((dirbuf(temp+10) and 80h) = 80h)` were both thrown away. Fixed in
upeepz80 0.2.4.

Source-built `.PRL` utilities produced no output on any nucleus, because the
image was linked one page below where MP/M loads it. `NUCLEUS/CLI.ASM` loads a
transient at `segment$bottom + 0100H`, but its `relocate` adds only the memory
segment's base *page* to the bytes the relocation bitmap marks, so the missing
page has to come from the link: a transient `.PRL` is linked at 0100H, exactly
like a `.COM`. `ul80 --prl` linked it at 0. DRI's own binaries state the
convention plainly — the highest relocatable word in `bin/dri/STAT.PRL` is its
program length plus 0100H, and `bin/src/STAT.PRL` used to stop at its program
length.

Page zero was the other half of it. Under MP/M page zero belongs to the
process's memory segment, so the BDOS entry at 0005H, the default FCB at 005CH
and the DMA buffer at 0080H have to be relocated with everything else; DRI's
`DIR.PRL` marks twelve `CALL 5` sites in its bitmap. Only a resolved symbol
reference can reach the bitmap, so the addresses must not be assembled as
literals. They now come from modules of their own: DRI's `X0100.ASM` for the
program's names (see Changed), and new `src/mpm_pagezero.mac` for the
compiler's own, the BDOS entry as `??BDOS` (DRI's `X0100.ASM` does not
publish `BDOS`, and `UTIL7/DM.PLM` declares a variable of that name); `.PRL`
targets are now compiled with `uplm80 --mode mpm`, which emits the BDOS call,
the stack fetch from 0006H and the warm-boot jump as those symbols instead of
literals. This is the same split DRI used: `PLM_WORK/X0100.ASM` and
`X0200.ASM` differ only in an `offset` equate, and GENMOD found the page-zero
references by diffing the two images. `.SPR` and `.RSP` output keeps its origin
of 0 through the new `ul80 --spr`, because those are loaded at the segment base
rather than a page above it.

`build_hd1k.sh` laid the distribution floppies down *after* the selected binary
tree, so the V2.1 originals overwrote every source-built file and `--tree=src`
silently produced a disk of V2.1 utilities on a V2.0 nucleus. The floppies are
now the base layer, as the comment there always claimed.

Disk transfers went to the last user bank selected rather than to the calling
process's. The emulator's READ and WRITE moved a record to or from the DMA
address in whichever bank 1-7 had been selected last, which is wrong for any
process whose buffers are in bank 0 — the banked half of a resident system
process. The SFTP RSP is one, which is why every HTTP file download answered
404 and every SFTP `get` failed with "No such file or directory" while
directory listings, which the BDOS reads into common memory, worked. DRI's
spooler is another: built from source and put in the system, it listed 36
records of zeros for a 36-record text file and so never stopped for STOPSPLR.
MP/M II has an interface for exactly this, and the XIOS now uses it: READ and
WRITE bracket the transfer with SWTUSER, which selects the bank of the process
the BDOS is working for, and SWTSYS (System Implementor's Guide 2.4), and the
emulator transfers in the bank selected when they call it. Checked on the DRI
tree and on `--tree=src` V2.0 and V2.1: HTTP serves `DUMP.ASM` byte for byte,
SFTP `get` returns `ABORT.PRL` unchanged, and a `put` round-trips.

A disk read or write that had succeeded could fail at random, with DISK READ
or DISK WRITE NONRECOVERABLE or "Bdos Err On A: Bad Sector". The XIOS
dispatches a call with `OUT (0E0H),A`, and used to read the result back with
`IN A,(0E0H)`, which returned a copy the emulator kept in one variable for the
whole machine. The 60 Hz tick is taken at any instruction boundary and ends in
the dispatcher, and whatever ran next — the dispatcher's own SELMEMORY, or the
SFTP RSP, which polls constantly — made XIOS calls of its own before the
interrupted process got to its IN, which then returned their result. Forty
`pip b:c.dat=b:big.dat[v]` copies of a 64K file in a row: the old emulator
reported NONRECOVERABLE on 7 of the first 11 and then hung; now 160 such
copies over three runs had no error. The handler leaves the result in A as
part of the OUT, so neither the XIOS (`asm/bnkxios.asm`) nor the SFTP RSP's
glue (`asm/sftp_glue.asm`) reads it again, and an `IN A,(0E0H)` returns A
itself. A is saved and restored with the rest of a process's registers, so it
still holds the result when the process runs again.

After an SFTP `put`, any other process that opened the file got "Bdos Err On
A: File Currently Open" until the system was restarted. The SFTP open for
writing made the file (BDOS 22) and opened it again into the same FCB, and the
close closed it once; under MP/M II a make and an open each put an entry on
the lock list and only a close takes one off, and an RSP never terminates, so
nothing ever removed the make's. A file put with no data at all was not closed
even once. `put` now returns only once MP/M has the file closed, so a command
typed straight after it can use it, and a write that fails is reported to the
client.

An SFTP upload could go into whatever file an HTTP read had open. The SFTP
RSP serves every SSH session and the HTTP server from one FCB, and a transfer
was a run of requests that carried the open file from each to the next, so
another client's request in between replaced it: a 200K `put` with HTTP
reading `DUMP.ASM` in a loop left `DUMP.ASM` as 435 bytes of the upload's data
and the uploaded file unreadable. Every request now stands on its own, and the
RSP keeps nothing from one to the next: a read opens the file, reads up to 15
records at the request's offset and closes it; a write does the same with
write random; a directory listing searches from the first entry and skips the
ones it has already returned. Under the same load `DUMP.ASM` is unchanged, all
60 HTTP reads of it made during the transfer are right, and the 200K file
comes back as it went up; so do a 120K and a 90K file put and fetched from two
sessions at once while two more list A: over SFTP and HTTP. A 200K `put` or
`get` takes about 4.7 seconds.

An HTTP read of a file ED had open hung a V2.0 system for good. The SFTP
RSP's open was refused ("File Currently Open"), and the RSP ran in the BDOS's
default error mode, so V2.0's RESBDOS printed the error through the XIOS with
the RSP's whole console byte, 0F0H: the XIOS polled device 0E0H for output
ready, which it never is, inside the BDOS, and every process that went near a
disk stopped behind it. V2.1's RESBDOS prints only for a process that owns the
console, so a V2.1 system just lost the message. The RSP now selects
return-error mode (BDOS function 45) when it starts; HTTP answers 404 for such
a file, and SFTP reports it missing.

A console program could not open a file an HTTP client was reading. The RSP
opened the files it read in MP/M's default, locked, mode, so `pip x=dump.asm`
with an HTTP loop on `DUMP.ASM` ended in
`ERROR - OPEN FILE INCOMPATIBLE MODE - A:DUMP.ASM`. A read now opens the file
read-only (F6'), as TYPE and PIP do. With three HTTP loops reading `DUMP.ASM`,
five PIPs and a TYPE of it on a console all succeed while 32 HTTP reads of it
return 200. A file a console program has open locked, as ED does, is still
refused, and writes keep the locked mode.

`submit` on a source-built system stopped the whole machine — no prompt on
any console, HTTP dead, the emulator at 100% CPU — and a command file of more
than about 1K came out garbled ("Bad entry"), with the rest of it lost.
`SUB.PLM` builds its command file in `rbuff`, declared `AT (.minimum$buffer)`
above `minimum$buffer`'s own declaration, and uses everything from there to
the top of its memory segment. um80 0.3.48 read the forward `EQU` uplm80
wrote for that as 0, so SUBMIT built the file over page zero, the tick's RST 1
vector included; and uplm80 put its string constants, its procedures' shared
locals and its stack after the last variable, where Intel's LOCATE put them
below the data. Both defects are fixed in the releases this one needs, and
`SUB.PLM` builds as DRI wrote it. On a source-built V2.0 system a 400-line
command file gives the same 402 user numbers, in the same order, as DRI's
SUBMIT.PRL, and a 200-line file of 120-character lines, 24K of commands, runs
to the end; a 300-line file using `$1` and `$2` prints the same 1202 lines as
DRI's SUBMIT.PRL, in V2.0 and V2.1.

`SPOOL.PRL` built from source wrote its message to the spooler RSP over the
queue's own pointer: `spool$msg` is declared `AT (.tbuff-1)`, which uplm80
compiled as the location counter, the queue control block itself. While no
system had a spooler RSP nothing noticed; with `SPOOL.RSP` in the system,
`spool file` stopped the emulator with an assertion or hung the machine.
Printing the files itself, with no spooler RSP, `SPOOL.PRL` also read records
into a buffer that ran over uplm80's locals, as SUBMIT's did: a text file came
out as garbage, and a file of zeros sent the records over SPOOL's own code
before it reached its `[D]` delete. Both are fixed in the releases this one
needs, and SPOOL now lists a file and, with `[D]`, deletes it, as DRI's
SPOOL.PRL does.

`ASM.PRL`, `RDT.PRL` and `DDT.COM` are made the way DRI made them.
`UTIL1/ASM.SUB` and `DDT.SUB` never link them: they assemble each module twice
with MAC, the second time with `+R`, which puts every ORG 100H higher, and
GENMOD builds the relocation map from the bytes that differ. `DDT.SUB` does it
twice over, with DDT0MOV, the relocator, loaded over RELDDT's header page, and
PRLCOM makes `DDT.COM` from `RDT.PRL`. Linked with ul80, none of the three
came out right: `RDT.PRL` was the bare module, with no relocator and no
relocation bits; `DDT.COM` began with DDT1ASM's `JMP 0683H` at 100H; and
`ASM.PRL` had not one of the 939 relocation bits DRI's has, so it ran only in
a memory segment based at 0000H. The build now assembles each module, and a
copy of it with every ORG 100H higher, with `um80 --aseg`, and the new
`tools/genmod.py` does what GENMOD, GENHEX and PRLCOM did. Under cpmemu DRI's
MAC.COM gives the same HEX records for all ten modules both ways. `genmod.py`
refuses the ORG form um80 assembles differently from MAC — a label on an ORG
line, which MAC sets to the new location and um80 to the old — rather than
let it through; none of the ten modules has one. GENMOD never cleared its
memory, so a DS area or
the gap before a module's ORG kept whatever the program run before it had
left there. The default build leaves those bytes zero; `--dri-exact` fills
them with MAC.COM, which makes `RDT.PRL` and `DDT.COM` identical to DRI's and
`ASM.PRL` identical but for 11 bytes of MAC's variables that MAC changed while
it ran. `verify_dri.py` compares all three, header and relocation map
included, against the V2.0 master `CONTROL` and against `mpm2dist`, which
carry the same three files. The copies next to the sources in `mpm2src/UTIL1`
are a rebuild neither master carries, and not a reference.

The source-built `GENSYS.COM` relocated every SPR with the wrong bit map. um80
0.3.48 assembled `LDRLWR.ASM`'s `mvi a,low(bitmap+128)` as the low byte of
`bitmap+128`'s offset within its module, with no relocation, so GENSYS read the
next record of an SPR's bit map at the wrong point. Run under cpmemu beside
DRI's with the same answers and SPR files, the V2.0 build wrote an `MPM.SYS`
that differed from DRI's in 5872 bytes, and the V2.1 build read a fourth bit
map record out of a file that has three. um80 0.3.49 hands `LOW()` of a
relocatable value to the linker, as MACRO-80 does, and `LDRLWR.ASM` is DRI's
text again but for V2.1's close of each file it loads: linked where DRI's V2.0
`GENSYS.COM` has it, it is DRI's 415 bytes. GENSYS now writes the same
`MPM.SYS` and `SYSTEM.DAT` as DRI's, but for the six serial number bytes. The
host build was never affected, since it generates the system with
`tools/gensys.py`.

`docs/ldrlwr_bug.md`, written for 0.3.4, and the README said that DRI's own
GENSYS relocates an SPR wrongly when its length is a multiple of 128 bytes,
and worst at 1024. It does not. DRI's V2.1 GENSYS.COM, run under cpmemu,
relocates RSPs of 128, 256, 512, 1008, 1024, 1152, 1536 and 1664 bytes exactly
as `tools/gensys.py` does, and DRI's V2.0 GENSYS.COM ones of 256, 1024 and 1536
bytes; the analysis had missed that `LdRl` resets its end marker when it
changes buffers. Both documents now say so, and the old analysis is kept,
marked as superseded.

`tools/gensys.py`, given more than seven user memory segments, dropped the
table entries past seven but still sized the user system call stacks for the
larger number. It now does what V2.1's GENSYS does, for either release: it
prints "*** Error Maximum Exceeded - 7 Assumed ***" and uses seven. The table
has eight entries and the first is MP/M's own.

The PL/M runtimes put the command drive at 005CH and the two passwords'
address and length (`PASS0`/`LEN0`, `PASS1`/`LEN1`) at 0080H and 0082H, inside
the default DMA buffer. DRI's `X0100.ASM`, linked into every MP/M II PL/M
transient, has them at 0050H-0056H, which MP/M II's CLI zeroes for a
transient. So DRI's ED, TYPE and ERAQ, which test `len0 <> 0`, never take
their password path, and the source-built ones read the command tail's length
there and took it whenever a file was named. The source build now links
`X0100.ASM` itself (see Changed), with DRI's values.

DUMP, the one transient written in assembler, is linked as DRI linked it,
`link dump,extrn[op]`, with `UTIL5/EXTRN.ASM` for its `bdos`, `fcb` and `buff`
and without the PL/M runtime. `DUMP.PRL`'s image and relocation bits are now
DRI's.

`ASM.PRL`, `ED.PRL`, `PIP.PRL`, `RDT.PRL` and `SDIR.PRL` put storage past the
end of their image, and nothing in an object file says how much. DRI named it
as GENMOD's third argument, and its binaries reserve 1000H for ASM, ED and PIP
and 1500H for RDT, in the `.PRL` header. The source build now asks for the
same. SDIR is the exception: V2.0's asks for none, as in both of DRI's V2.0
binaries and `UTIL7/SDIR.SUB`, and V2.1's for 1000H (see Added).

`tools/build.py` assembled the runtime modules only when their `.rel` did
not exist yet, so once a checkout had built anything, an edit to a runtime
changed nothing and nothing said so. Each runtime module is now assembled
once per run.

On macOS a fresh checkout built an emulator that linked cleanly and then would
not start: "dyld: Library not loaded: /usr/local/lib/libqkz80.4.dylib". With
`-L` pointing at `cpmemu/src`, the linker preferred the dylib there to
`libqkz80.a`, and the dylib's install name is where nothing is unless cpmemu's
`make install` was run. CMake now links the sister directory's `libqkz80.a` by
path. Linux CI builds only the archive, which is why it never saw this.

`bin/dri/TMP.SPR` was this repository's own build rather than Digital
Research's, and 128 bytes longer than every DRI copy. Replaced with the one
from the distribution.

### Known issues

A `--version=2.0` build carries V2.1's banked BDOS. The `BNKBDOS.ASM` DRI
shipped with the V2.0 sources is already V2.1's — it builds DRI's V2.1
`BNKBDOS.SPR` byte for byte — and rebuilding V2.0's would mean undoing DRI's
fixes across 26 regions of it, which has not been done.

The transients are compiled by uplm80, not by DRI's PL/M-80, so none of them
is byte for byte DRI's, in either release; the V2.1 ones were checked by what
they do. uplm80's code is larger, which matters only for the `.BRS` files, in
bank 0: `SCHED.BRS` is 06ABH bytes against DRI's 043DH.

`tools/gensys.py` does not write the same file as DRI's GENSYS for the same
answers, although it places and relocates every module the same way. It also
writes the LCKLSTS and CONSOLE DAT pages, as zeros, which DRI's leaves out of
the file, so its record count (system data bytes 120-121) is larger; it
leaves the size and bank of unused memory segment entries zero; and it pads
each module's last page with zeros where DRI's leaves whatever its sector
buffer held.

The emulator has no list device: the XIOS discards list output, so a spooled
file goes nowhere. With `[D]` the spooler still deletes it once it has been
listed, which is what the RSP test checks.

The `basic` and `stat` tests still check only that a prompt came back. The
new `rsp`, `http` and `sftp` tests check what the programs print and what
reaches the disk.

## [0.3.5] - 2026-09-23

### Fixed

The nucleus linked the CP/M runtime it never calls, and that alone broke
GENSYS. XDOS, BNKXDOS, RESBDOS and BNKBDOS are pure assembly, but the build
linked `cpm_runtime.rel` — the PL/M helper set, `??move`, `??div16` and friends
— into each of them. That added 18 bytes to every one of these modules, and 18
bytes is enough to carry a module past a 256-byte page boundary, so each one
paid a whole extra page. The accumulated pages pushed BNKXIOS below the
configured common base and GENSYS refused to lay the system out at all:

    Error: XIOS common base BF4BH below configured common base C000H

With the runtime skipped, every system module is exactly the program length of
the DRI original — XDOS 8700, BNKXDOS 497, RESBDOS 3072, BNKBDOS 8960, TMP
1040. TMP already carried `skip_runtime`, which is why it alone matched DRI.
For anyone bisecting this: the symptom only appeared once ul80 fixed DSEG
placement (um80 `e13fe21`). Before that the linker wrote each module's
initialized DSEG immediately after that module's own CSEG, on top of the next
module's code, which produced an XDOS.SPR short enough to fit and corrupt.

An SSH session could wedge before authentication and show the client nothing at
all. `ssh_handle_key_exchange()` is blocking and reads from the socket; on a
fast link it frequently pulls the client's next packet — the 52-byte
`SSH_MSG_SERVICE_REQUEST` — off the wire along with the last KEX packet. The
server callbacks were registered only after the key exchange returned. libssh
does parse and dispatch that second message from inside the key exchange; what
is lost is the reply. `messages.c ssh_message_queue()` sends the default
`SERVICE_ACCEPT` only when `session->server_callbacks` is non-NULL, and
otherwise parks the message on `session->ssh_message_list`, which this emulator
never drains because it uses the callback API and never calls
`ssh_message_get()`. So the request was answered by nobody, the client blocked
for ever waiting for `SERVICE_ACCEPT`, and the session spun in AUTHENTICATING.
The user saw no banner, no prompt and no response to a keypress, because the
banner is only sent once the shell request arrives — silence indistinguishable
from a hung guest OS.

The callbacks and the auth methods are registered in the constructor now, so
the handlers exist by the time those buffered bytes are parsed. Measured over
120 sequential sessions against each build, with the session's FIONREAD logged
at the instant KEX returns: before, 108 sessions reached MP/M and 12 were
silent; after, 120 of 120 reached MP/M and none were silent. The precondition —
nothing left to read at `kex_done`, meaning the SERVICE_REQUEST was already
swallowed into libssh's buffer — is not rare: it occurred in 63% of sessions
before the change and 72% after, so the wedge is gone despite the race being
entered more often. It needs a reasonably busy machine to show at that rate; an
earlier run of 80 sessions on an idle box produced none at all. This is the
failure that had been making `run_tests.sh` fail at a different point on every
run, at about the same 10% rate.

The test harness always tried to open the HTTP server on port 8000.
`run_tests.sh` started the emulator without `-w`, so anything else on the
machine already holding 8000 — another emulator, a stray instance from an
earlier run — made the emulator exit at startup with `Failed to start HTTP
server`, and every test in the run then failed with `SSH server did not start
within 15 seconds`. The symptom looks nothing like the cause. `HTTP_PORT` now
defaults to `PORT + 6000` and both are overridable, so a run can be pointed
elsewhere without editing the script.

The harness also never sent the command arguments its tests are named for.
`run_expect_test` passed its commands unquoted, so the shell split them on
whitespace and each word became a separate command: `stat a:` sent `stat` and
then `a:`. They are passed through as `"$@"` now. With the argument actually
arriving, `dir a:` came back `Bad entry`, because the prompt regex can match
part way through the echo of the previous line — expect typed while MP/M was
still draining its console and the first characters were lost. A short settle
before each send fixes that. The DIR test itself asked for a bare `dir`, which
prints nothing in MP/M II since DIR wants a drive or a pattern, so it passed
for a long time without ever listing anything; it asks for `dir a:` now.

### Added

A handshake watchdog closes a session stuck before authentication and says so.
Twenty seconds without authenticating now closes the session and logs the state
it was stuck in:

    handshake did not complete within 20s (state=1, kex_done=1); closing session

`state=1` is AUTHENTICATING with the key exchange already done, which names the
failure class above exactly. Measured against a build with the pre-fix callback
ordering deliberately restored so that stalls happen again: 27 of 40 sessions
stalled and the watchdog reported all 27. Against the current build: 40 of 40
sessions healthy, no false firings. Draining libssh's legacy `ssh_message`
queue from the AUTHENTICATING case was considered as a second safety net and
deliberately left out — the code there is an empty break with a comment warning
that `ssh_message_get()` would consume messages the callbacks are meant to
handle, and with the ordering fixed the queue should never fill.

`docs/source_build_dependencies.md` now states precisely why `--tree=src` does
not produce a working system, with the measurements behind it. It looks like a
build bug and is not; see Known issues.

### Changed

`bin/src` is rebuilt with the corrected nucleus link and uplm80 0.3.5. Every
system module now has exactly the program length of the DRI original, where
before each pure-assembly module carried 18 bytes of runtime it never used and
paid a page for them. XDOS.SPR also grows from 8960 to 10112 bytes on disk,
matching DRI's file size — the old image was short because of the DSEG overlap
described above, so it is now complete rather than overlapping. ED.PRL is in
the rebuild: uplm80 could not compile ED.PLM at all between its front-end
migration and 0.3.5, because a PROCEDURE declared at the head of a `DO` block
was emitted inline with nothing jumping over it, so this is the first `bin/src`
in which ED comes from the current compiler. A system built `--tree=src` now
reaches a command prompt, where before it stopped at the console banner.

Two clang 21 warnings are cleared. `banked_mem.cpp` keeps `is_instruction` in
`fetch_mem`'s signature because it overrides `qkz80_cpu_mem::fetch_mem`, but
drops the parameter name and records the reasoning: MP/M II's SELMEMORY latch
switches the whole banked region for the running process and is blind to the
kind of bus cycle, and a `.PRL` image carries code and data in one bank, so an
opcode fetch and a data read at the same address must return the same byte. The
flag is also not an /M1 signal — qkz80 passes true for every byte pulled from
the PC stream, including immediate operands and DD/FD displacements. In
`xios.cpp`, a static `call_count` that was incremented but never read is gone;
it was checked first for a periodic log line or action it might have gated, and
had no remaining reader.

`CPACK_PACKAGE_HOMEPAGE_URL` named `github.com/wohl/mpm2`, which 404s, and that
value is baked into the `.deb` and `.rpm` metadata. Both README links to the
MP/M II Command Reference also 404'd after `docs/mpm2_summary.pdf` moved to the
retro_docs archive, and the install instructions still used the 0.3.0 package
filenames rather than the `mpm2-emu-0.3.4-Linux.deb`/`.rpm` the v0.3.4 release
actually ships. A Related Projects section was added to the README and later
rewritten under the ASD-STE100 Issue 9 rules, using one canonical name per
project across the repositories.

### Known issues

**Superseded by 0.3.6 — this explanation was wrong.** It is kept here as
written because it was released under this version.

`--tree=src` still does not produce a usable system, and it cannot be fixed
from this repository. `mpm2_external/mpm2src/NUCLEUS` is MP/M II **V2.0** —
`VER.ASM` says so, and a source-built system banners as "MP/M II V2.0", 1981 —
while everything in `bin/dri` is **V2.1**, 1982. A system built from source is
therefore a genuine V2.0 MP/M II, and the V2.1 utilities on the disk do not run
on it.

The part of that which survives measurement is only that the nucleus sources
are V2.0. The utilities were not the problem: they were linked one page below
where MP/M loads a `.PRL` and would not run on a V2.1 nucleus either. See
0.3.6.

The SSH tests still assert only that a prompt came back, not that the command
produced the right output. The prompt regex matches the prompt echoed with the
command itself, so a test can pass while the program printed nothing at all —
which is exactly how the bare `dir` passed for as long as it did. Asserting on
output needs the expect script to match the command's own text first and then
its output, which is a larger change than this release made.

## Earlier releases

No release notes were written for these; the summaries are reconstructed from
the commits between the tags, and the dates are the tag dates.

## [0.3.4] - 2026-01-08

`mkboot.cpp` was removed in favour of `cpm_disk.py write-boot`, which already
did the same job. The source build test was added to the CI workflow, the
release workflow's version numbers were corrected, and the relocation bug in
DRI GENSYS's handling of `LDRLWR.ASM` was written up.

## [0.3.3] - 2026-01-07

A source build test was added and the committed binaries were rebuilt with a
fixed uplm80. Documentation caught up with the listener and IPv6 work from
0.3.2.

## [0.3.2] - 2026-01-07

Multiple listeners for the HTTP and SSH servers, with IPv4 and IPv6 listen
addresses. Startup now fails if a configured HTTP or SSH port cannot be bound,
rather than continuing without it.

## [0.3.1] - 2026-01-07

SSH console reconnection was fixed and the test harness was made to survive it:
console cleanup between SSH tests, one command per session, an absolute path
for the SSH host key, and host key generation in CI. A CI workflow runs the
tests on push.

## [0.3.0] - 2026-01-07

The earliest tag in the repository. By this point the emulator had SSH and SFTP
reworked onto libssh's callback API with a thread per connection, access
logging for HTTP, SSH and SFTP, a read-only HTTP file browser, a Python
replacement for DRI's GENSYS, documentation of the source build dependencies,
and a GitHub Actions workflow producing `.deb` and `.rpm` packages.