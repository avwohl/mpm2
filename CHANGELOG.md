# Changelog

Notable changes to mpm2, a Z80 MP/M II emulator with SSH terminal access.

This file begins at 0.3.5. Releases 0.3.0 through 0.3.4 were tagged without
release notes; they are summarised below from their commits, in less detail
than they would have carried at the time, so the record before 0.3.5 is short
rather than empty.

## [Unreleased]

### Added

MP/M II V2.1 can be built from the same tree as V2.0. DRI published sources
for V2.0 only, so the V2.1 changes were recovered from the binaries and put
into `src/overrides` behind `IFDEF MPM21` / `$if MPM21`:

- `./scripts/build_all.sh --tree=src --version=2.1` builds the V2.1 system,
  which boots and reports `MP/M II V2.1 / Copyright (C) 1982, Digital
  Research`.
- `tools/verify_dri.py` builds both releases with `--dri-exact` and compares
  them against DRI's own binaries. XDOS.SPR, BNKXDOS.SPR, RESBDOS.SPR and
  TMP.SPR all come back byte for byte identical, in both releases.
- `tools/build.py` gained `--version`, `--serial`, `--dri-exact` and
  `--output-dir`; `build_all.sh` passes the first three through.
- The reconstruction, the binary evidence behind each change, and the four
  transients that are identified but not yet reconstructed (SDIR, SPOOL, PIP,
  GENSYS) are written up in `docs/mpm2_v21.md`.

### Fixed

- `bin/dri/TMP.SPR` was this repository's own build rather than Digital
  Research's, and 128 bytes longer than every DRI copy. Replaced with the one
  from the distribution.

## [0.3.6] - 2026-09-23

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
  used.

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
entry. Fixed in uplm80 0.3.7.

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
as `EQU 0005H` — but uplm80 passes arguments to an external `PROCEDURE` on the
stack. It open-codes `MON1` and `MON2` when the function number is a constant,
which is why this went unseen: the routines were only reached when it could
not, and then they ran on whatever `C` and `DE` happened to hold. `DIR.PLM`'s
`parse` (XDOS 152, through `mon3`) was one such call, so DIR parsed its command
line from garbage. Both runtimes now take the arguments from the stack, and all
four are one routine: the BDOS returns a byte in `A` and an address in `HL`,
which is what uplm80 reads for a `BYTE` and an `ADDRESS` result.

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
literals. New `src/mpm_pagezero.mac` publishes them from a module of their own,
and `src/mpm_runtime.mac` reaches them across that module boundary; `.PRL`
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

### Changed

`--tree=src` produces a running system. A fully source-built MP/M II V2.0 boots,
loads and runs transient programs, and every PL/M utility tried — `dir`, `sdir`,
`stat`, `tod`, `user`, `console`, `show`, `type`, `dump`, `set`, `prlcom`,
`printer`, `stopsplr`, `sched`, `spool`, `abort`, `ren`, `submit`, `mpmstat`,
with and without arguments — gives the same output as DRI's own binaries — `stat` prints
`A: RW, Space:     7,512k`, `dir` lists `A: $3$      SUP`, `tod` prints
`Mon 09/14/81 00:00:19`, `user 0` prints `User Number = 0`, `console` prints
`Console = 3`. Before this release a source-built system printed a program's
load line and then dropped the session, whatever the program was.

### Known issues

The V2.0 nucleus sources here are not the V2.1 binaries in `bin/dri`, and V2.1
looks like V2.0 plus in-place patches rather than a recompile: every nucleus
module has the same program length in both trees, `PATCH.ASM`'s 128 reserved
zero bytes are filled with code in DRI's `XDOS.SPR`, and V2.0 call sites are
rewritten to call into that area — at program offset 0x01F8 V2.0's `lxi h,0016 /
dad d / mov m,b` becomes `call 1814H`, and at 0x0527 `lhld 2081H` becomes
`call 183FH`. About 56 bytes of real code differ, the rest being those patch
areas and the serial number. A source-built system is therefore a genuine V2.0
and does not carry DRI's later fixes. `BNKBDOS.SPR` and `TMP.SPR` build
byte-identical to DRI's.

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