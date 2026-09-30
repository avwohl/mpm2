# Testing

`scripts/run_tests.sh` starts the emulator on the current disk image, runs
tests against it over SSH, SFTP and HTTP, and stops it again:

```bash
./scripts/build_all.sh && ./scripts/run_tests.sh all   # the DRI tree
./scripts/run_tests.sh src                             # builds --tree=src itself
PORT=2311 ./scripts/run_tests.sh all                   # SSH on 2311, HTTP on 8311
```

| Test | Checks |
|------|--------|
| `basic` | `dir a:` over SSH |
| `stat` | `stat` and `stat a:` |
| `rsp` | `scripts/test_rsp.exp`: the resident system processes - MPMSTAT lists their queues, SCHED runs a command that is due, ABORT answers through its queue, SPOOL lists and deletes a file (one PIP made, checked with DIR first) and STOPSPLR stops it |
| `http` | a file read over HTTP, through the SFTP RSP |
| `sftp` | `scripts/test_sftp.exp`: files put over SFTP read back over SFTP and HTTP, then open from a console - `type` one, `submit` the other; `pip` copies a file HTTP is reading; HTTP is refused a file `ed` has open, and the system carries on; and a 64K file goes up and comes back intact while HTTP reads another file, which stays intact too |
| `all` | all of the above |
| `src` | `build_all.sh --tree=src` (V2.0), then `basic`, `rsp`, `http` and `sftp`; a build that fails fails the test |
| `interactive` | an SSH session to type at |

The SSH port is `PORT` (default 2222) and the HTTP port `HTTP_PORT` (default
`PORT` + 6000), so two checkouts can run their tests at once on different
ports.  Logs go to `build/mpm2_test.log` and `build/mpm2_src_build.log`, and
`gensys.sh` generates the system in `build/gensys_work` (or `$GENSYS_WORK`),
so nothing is shared through `/tmp`.  A console prompt is waited for 30
seconds (45 in `basic` and `stat`); the 64K SFTP transfer, which runs through
the Z80 RSP a record at a time and so at the emulated machine's speed, is
given 180.  `src` rebuilds `bin/src` (see
[Building from Source](building.md#building-from-source)).

`python3 tools/verify_dri.py` builds the nucleus, BNKBDOS, `ABORT.RSP`,
`DUMP.PRL`, `GENHEX.COM`, `GENMOD.COM`, the assembler, the debugger and the
loader of both releases with `--dri-exact` and compares them with Digital
Research's binaries.
