# Building MP/M II

## Prerequisites

### Required Dependencies

| Dependency | Purpose | Installation |
|------------|---------|--------------|
| CMake 3.16+ | Build system | `brew install cmake` or `apt install cmake` |
| C++17 compiler | Compile emulator | Xcode (macOS) or `apt install g++` |
| Python 3.10+ | Build scripts, um80/ul80, uplm80 | Usually pre-installed |
| cpmemu 4.10.0 | Z80 CPU library (`libqkz80`) and disk tool (`util/cpm_disk.py`) | Clone from github.com/avwohl/cpmemu and run `make` in `cpmemu/src` |

### External Repositories (must be cloned separately)

```bash
# Clone these as siblings to mpm2/
cd ~/src  # or wherever you keep source

# Z80 CPU emulator (required)
git clone https://github.com/avwohl/cpmemu.git

# um80/ul80 - MACRO-80 compatible assembler/linker (required)
git clone https://github.com/avwohl/um80_and_friends.git
cd um80_and_friends
pip install -e .  # Installs um80 and ul80 commands
cd ..

# uplm80 - PL/M-80 cross-compiler (required: the SFTP RSP, and --tree=src)
git clone https://github.com/avwohl/uplm80.git
cd uplm80
pip install -e .  # Installs uplm80 command
cd ..

# MP/M II distribution files are included in the mpm2_external/ directory
```

The toolchain is also on PyPI: `pip install um80 uplm80` installs
um80_and_friends (the `um80` package: um80 and ul80), uplm80 and the upeepz80
it uses. This release needs:

| Tool | Release | Needed for |
|------|---------|------------|
| uplm80 | 0.4.0 or later | every build (the SFTP RSP is PL/M) and `--tree=src` |
| upeepz80 | 0.2.6 or later | the same: it is uplm80's peephole optimizer |
| um80_and_friends | 0.3.51 or later | every build (LDRBIOS, BNKXIOS, the SFTP RSP) and `--tree=src` |
| cpmemu | 4.10.0 | every build (the emulator's Z80 and the disk image) |

Older releases get parts of the system wrong, or cannot build it. uplm80
before 0.4.0 does not pass a procedure's arguments the way PL/M-80 does, in
BC and DE, so what it compiles does not work with DRI's own interface
modules - `X0100.ASM`, `BRSPBI.ASM`, `LDMONX.ASM` - which the build links as
DRI's submit files did, and the SFTP RSP's glue takes its arguments in
registers too; and uplm80 0.4.0 refuses an upeepz80 before 0.2.6, which
turned `push ... / call p / ret` into `jp p`, so that p took its return
address for its first argument. um80 before 0.3.51 has no `--dri`, with
which the source build reads every one of DRI's assembler sources the way MAC
and RMAC read them, and ul80 before 0.3.51 no `--fatal-mult-def`, which every
link `tools/build.py` makes passes, and the BNKXIOS and SFTP links too;
before 0.3.50 um80 cannot give DRI's sources RMAC's six-character PUBLIC and
EXTRN names (`-t`), without which the nucleus does not link. An older uplm80
miscompiles DRI's own text of SUBMIT, SPOOL and the resident system
processes, and makes a source-built SDIR repeat its heading before every
line; and an older ul80 leaves `.MEMORY` out of a `.PRL`'s relocation bit
map. The [CHANGELOG](../CHANGELOG.md) has the details.

### Optional: SSH Support

For network access via SSH (recommended for multi-user):

```bash
# macOS
brew install libssh

# Linux (Debian/Ubuntu)
sudo apt install libssh-dev
```

## Building

The project supports two binary trees:

| Tree | Description | Requirements |
|------|-------------|--------------|
| `dri` | Original DRI binaries (default) | um80, ul80 and uplm80 for the emulator's own XIOS and SFTP RSP |
| `src` | Build from source code | the same |

```bash
cd mpm2

# Build with DRI binaries (fast, recommended)
./scripts/build_all.sh

# Build from source (compiles PL/M and assembly)
./scripts/build_all.sh --tree=src
```

Build steps:
1. **[src only] build_src.sh** - Compile source code to bin/src/
2. **build_hd1k.sh** - Creates 8MB disk image with binaries from selected tree
3. **build_sftp_rsp.sh** - Compiles the emulator's SFTP resident system process (`SFTP.RSP`, `SFTP.BRS`)
4. **build_asm.sh** - Assembles LDRBIOS and BNKXIOS, builds C++ emulator, writes boot sector
5. **gensys.sh** - Runs GENSYS to create MPM.SYS: 4 consoles, 7 memory banks and five
   [resident system processes](#resident-system-processes)

Output: `disks/mpm2_system.img` - bootable disk with MP/M II

### Building from Source

When using `--tree=src`, all utilities are compiled from the original Digital Research
source code using modern cross-compilers:

- **uplm80** - PL/M-80 to Z80 assembly compiler
- **um80** - MACRO-80 compatible assembler
- **ul80** - LINK-80 compatible linker

The source build system supports local modifications in `src/overrides/` that take
precedence over the original source. For example, the MPMLDR has its serial number
check disabled in `src/overrides/MPMLDR/MPMLDR.PLM`, except with `--dri-exact`,
which keeps DRI's check and builds DRI's serial number into the loader and the
nucleus alike. The overrides also carry the V2.1 changes, behind `MPM21` (see
[below](#mpm-ii-v20-and-v21)).

`build_src.sh` - and so `build_all.sh --tree=src` and `run_tests.sh src` - writes
what it builds to `bin/src/`, over the committed binaries there. Those are
V2.0's, the default, built with the toolchain releases above.

The assembler (`ASM.PRL`) and the debugger (`RDT.PRL`, `DDT.COM`) are not
linked: DRI built them with MAC and GENMOD (`UTIL1/ASM.SUB`, `DDT.SUB`), each
module assembled twice, the second time 100H higher, and GENMOD taking the
relocation bits from the bytes that differ. The build does the same - `um80
--aseg` for the two assemblies, and `tools/genmod.py` for GENMOD, GENHEX and
PRLCOM. DRI made GENHEX and GENMOD themselves with MAC and LOAD
(`UTIL3/GENHEX.SUB`, `GENMOD.SUB`), and `genmod.py` does what LOAD did too.

Every one of DRI's assembler sources is assembled with `um80 --dri`, which
reads it as MAC and RMAC do: a `$` inside a name is ignored (DRI's text
spells some names two ways - `MPM.ASM` stores to `nmb$lst`, which
`DATAPG.ASM` defines as `nmblst`), a label needs no colon, `PUSH A` is
`PUSH PSW`, a line that starts with `*` is a comment, and a `!` ends a `;`
comment and starts the next statement. So they build from
DRI's text: `BNKBDOS.ASM`, `MPMLDR/LDRBDOS.ASM` and `LDRBIOS.ASM`, and the
nucleus modules V2.1 did not change, as they stand; the nucleus modules it
did change from overrides that are DRI's text but for the V2.1 changes, the
serial number, and in `TMPSUB.ASM` the one local fix `--dri-exact` leaves
out. `MPMLDR.COM` is put together as DRI's `MPMLDR.SUB` did it, the PL/M
loader at 0100H, the loader's BDOS at 0D00H and DRI's skeleton loader BIOS at
1700H. Every link `tools/build.py` makes, and the BNKXIOS and SFTP links,
stops at a name two modules define (ul80 `--fatal-mult-def`); the loader
BIOS and the boot sector are one module each.

The PL/M programs link with DRI's own interface modules, unmodified, as
DRI's submit files linked them: every `.PRL`, and `LOAD.COM`, with
`PLM_WORK/X0100.ASM`,
whose `MON1` to `MON3` are `equ 0005h` and whose `FCB`, `TBUFF` and the rest
are the page-zero addresses; every `.BRS` with `UTIL2/BRSPBI.ASM`, which
reaches the BDOS through the `.RSP`; `MPMLDR.COM` with `MPMLDR/LDMONX.ASM`,
whose `LDMON1` and `LDMON2` are the loader BDOS at 0D06H; `GENSYS.COM` with
`MPMLDR/X0100.ASM`; and `DUMP.PRL` with `UTIL5/EXTRN.ASM`. uplm80 passes a
call's arguments as PL/M-80 does, the last in DE and the one before it in
BC, so the BDOS gets the function in C and the parameter in DE with nothing
in between. `src/mpm_pagezero.mac` and `src/brs_runtime.mac` add only the
names uplm80 gives its own references to the BDOS, the stack top and the
warm-boot jump.

With `--tree=src`, the entire MP/M II operating system is built from source. Only 4
development tools are binary-only (no source available):

| Binary | Purpose | Note |
|--------|---------|------|
| RMAC.COM | Relocatable Macro Assembler | Replaced by um80 |
| LINK.COM | Linker | Replaced by ul80 |
| LIB.COM | Library Manager | Not needed for build |
| XREF.COM | Cross Reference | Not needed for build |

These are included on the disk for completeness but are not used in the build process.

To build just the source binaries without creating a disk:
```bash
./scripts/build_src.sh
```

### MP/M II V2.0 and V2.1

`mpm2src.zip` is the V2.0 source release; DRI never published V2.1 sources.
The V2.1 changes have been recovered from the binaries and put into
`src/overrides` behind `IFDEF MPM21`, so either release builds from the one
tree:

```bash
./scripts/build_all.sh --tree=src --version=2.1   # or 2.0, the default
python3 tools/verify_dri.py                       # both, against DRI's binaries
```

All four nucleus SPRs — XDOS, BNKXDOS, RESBDOS and TMP — come back byte for
byte identical to Digital Research's own V2.0 and V2.1 binaries, the padding
of their last record included, and so do BNKBDOS (V2.1's), the resident
`ABORT.RSP`, `DUMP.PRL`, the debugger, `RDT.PRL` and `DDT.COM`, and the
assembler, `ASM.PRL`, but for 11 bytes of it that no source sets and GENMOD
took from the memory MAC had left. So do `GENMOD.COM` and the part of
`MPMLDR.COM` that DRI assembled with MAC, the loader's BDOS and the skeleton
of its BIOS at 0D00H-177FH, DS areas included: LOAD wrote a byte no statement
loads from its 256-byte buffer, as the byte 256 below it, and so does the
build. `GENHEX.COM` does but for 70 bytes no source sets: 64, its stack,
held what LOAD's buffer found in memory, and 6 are zero in DRI's file where
LOAD of the source writes the bytes 256 below.

Outside the nucleus, V2.1 changed MPMLDR, SHOW, PRINTER, SCHED.RSP, SPOOL.PRL,
SPOOL.BRS, SDIR, PIP and GENSYS, and all of them are reconstructed. They are
compiled PL/M, which uplm80 cannot make byte for byte what DRI's PL/M-80 made,
so each was checked against DRI's patch instruction by instruction, and the
larger ones also by running them beside DRI's binary: the V2.1 PIP, for
instance, matches DRI's on every console line and every output file of 23
commands. BNKBDOS needs no reconstruction: the `BNKBDOS.ASM` DRI shipped with
the V2.0 sources is already V2.1's, so a V2.0 build gets the V2.1 banked BDOS
too. See [docs/mpm2_v21.md](mpm2_v21.md) for every change between the
releases and the evidence for it.

V2.1's GENSYS asks one question V2.0's does not: "Enable Compatibility
Attributes (N) ?".  The answer goes in system data byte 96, and with it set
the V2.1 CLI copies a command file's F1'-F4' attributes into the process
descriptor of the program it loads (DRI's "MP/M II Release 2.1 Compatibility
Attributes" addendum).  MPM.SYS is generated by `tools/gensys.py`, which
answers N, as DRI's GENSYS does by default; `--compat-attributes` answers Y:

```bash
./scripts/build_all.sh --tree=src --version=2.1 --compat-attributes
./scripts/build_all.sh --compat-attributes        # the DRI tree is V2.1
```

It needs a V2.1 XDOS - the DRI tree, or `--tree=src --version=2.1`.  A V2.0
XDOS never reads the byte, and `gensys.py` leaves it zero there with a note.

`build_all.sh` options:

| Option | Meaning |
|--------|---------|
| `--tree=dri\|src` | Binaries to build the system from (default `dri`) |
| `--version=2.0\|2.1` | Release to build from source (default 2.0); `--tree=src` only |
| `--compat-attributes[=yes\|no]` | Answer to V2.1 GENSYS's "Enable Compatibility Attributes" (default no) |
| `--serial=none\|dri` | Serial number in a source-built nucleus and MPMLDR: the sources' `654321` placeholder (default) or the one on DRI's master; `--tree=src` only |
| `--dri-exact` | Build what DRI shipped: DRI's serial number, without the local fixes `src/overrides` keeps behind `DRIEXACT` (MPMLDR checks the serial numbers, as DRI's does), and with what DRI's GENMOD found in memory (MAC.COM) in the bytes of ASM, RDT and DDT no source sets; `--tree=src` only |

### Modern GENSYS

`scripts/gensys.sh` generates `MPM.SYS` with `tools/gensys.py`, a Python
replacement for DRI's interactive GENSYS.COM that runs on the host:

- It reads its answers from JSON, which `gensys.sh` writes, instead of
  prompting.
- It places and relocates the modules as DRI's GENSYS does, including RSPs
  with a banked half (a `.BRS`, loaded when the RSP's process descriptor is
  in memory segment 0, which is how DRI's GENSYS decides).
- It makes the two V2.1 GENSYS changes that reach the system: the
  compatibility attributes question (system data byte 96, above), and at most
  seven user memory segments.

For the same answers, DRI's V2.0 and V2.1 GENSYS.COM (run under cpmemu) and
`gensys.py` place and relocate every module identically. The files are not
byte for byte the same: `gensys.py` also writes the LCKLSTS and CONSOLE DAT
pages, as zeros, which DRI's leaves out of the file (so the record count at
system data bytes 120-121 is larger); it leaves the size and bank of unused
memory segment entries zero; and it pads each module's last page with zeros
where DRI's GENSYS leaves whatever its sector buffer held.

Until 0.3.6 the README said that DRI's GENSYS relocates an SPR wrongly when
its length is a multiple of 128 bytes. It does not; the fault was in a
GENSYS.COM built from source with an older um80. See
[docs/ldrlwr_bug.md](ldrlwr_bug.md).

### Resident System Processes

Every system `gensys.sh` generates, from either tree, carries five resident
system processes (RSPs).  The `.RSP` half of each is loaded into common
memory below the XDOS; a banked RSP's code is in a `.BRS` of the same name,
loaded into bank 0.

| RSP | Files | Used by |
|-----|-------|---------|
| ABORT | `ABORT.RSP` | `abort` - the CLI sends the line to the ABORT queue ("Msg Qued") |
| MPMSTAT | `MPMSTAT.RSP`, `.BRS` | `mpmstat` |
| SCHED | `SCHED.RSP`, `.BRS` | `sched`, which hands the request to it |
| SPOOL | `SPOOL.RSP`, `.BRS` | `spool` and `stopsplr`, through the SPOOLQ and STOPSPLR queues |
| SFTP | `asm/SFTP.RSP`, `SFTP.BRS` | the emulator's [SFTP](file_transfer.md#sftp-file-transfer) and [HTTP](file_transfer.md#http-file-browser) file access |

The first four come from `bin/dri` or `bin/src`, as selected with `--tree`;
the SFTP RSP is the emulator's own, built from `asm/`.  There is no
printer: the XIOS discards list output, so a spooled file goes nowhere
(with `[D]` the spooler still deletes it once it has been listed).
