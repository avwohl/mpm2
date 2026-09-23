# Changelog

Notable changes to mpm2, a Z80 MP/M II emulator with SSH terminal access.

This file begins at 0.3.5. Releases 0.3.0 through 0.3.4 were tagged without
release notes; they are summarised below from their commits, in less detail
than they would have carried at the time, so the record before 0.3.5 is short
rather than empty.

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

`--tree=src` still does not produce a usable system, and it cannot be fixed
from this repository. `mpm2_external/mpm2src/NUCLEUS` is MP/M II **V2.0** —
`VER.ASM` says so, and a source-built system banners as "MP/M II V2.0", 1981 —
while everything in `bin/dri` is **V2.1**, 1982. A system built from source is
therefore a genuine V2.0 MP/M II, and the V2.1 utilities on the disk do not run
on it. Measured on the same disk and the same emulator with only MPM.SYS
regenerated, an all-DRI system runs `stat` in 6 of 6 sequential sessions and an
all-src system produces no output in 6 of 6. The difference is not in one
module: substituted singly into an otherwise all-DRI system and counting
sessions that complete a `dir`, src `BNKBDOS.SPR` passes 8 of 8 and src
`TMP.SPR` 12 of 12 (it is byte-identical in both trees), but src `XDOS.SPR`
passes 2 of 10, src `RESBDOS.SPR` 0 of 8 and src `BNKXDOS.SPR` 1 of 8 — so
installing DRI's V2.1 `XDOS.SPR` alone does not fix it, and `RESBDOS` or
`BNKXDOS` alone each reproduce it. One concrete instance of the delta: V2.1
hides a six-byte routine (`dcx b / ldax b / ani 0Fh / mov c,a / ret`) in
`BNKXDOS` ProcAddressTable slots 2-4 at program offset 0x08, which the V2.0
sources do not have. Repairing this needs V2.1 nucleus sources, which are not
in this repository. Until then `--tree=src` is useful for checking that the
toolchain builds everything, not for producing a runnable system; use
`--tree=dri` for that.

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