# MP/M II Emulator

A Z80-based MP/M II emulator with SSH terminal access. Multiple users can connect simultaneously to run CP/M-compatible software.

**[MP/M II Command Reference](https://github.com/avwohl/retro_docs/blob/main/mpm2/mpm2_summary.pdf)** - Complete guide to all commands and utilities

## Quick Start

```bash
# Build with DRI binaries (default, fast)
./scripts/build_all.sh

# Or build from source (requires uplm80/um80/ul80)
./scripts/build_all.sh --tree=src                 # MP/M II V2.0
./scripts/build_all.sh --tree=src --version=2.1   # MP/M II V2.1

# Run with local console
./build/mpm2_emu -l -d A:disks/mpm2_system.img

# Or run with SSH access (connect from another terminal)
./build/mpm2_emu -d A:disks/mpm2_system.img
ssh -p 2222 user@localhost
```

## Binary Installation

Pre-built packages are available for Linux systems. Download the appropriate package and disk image from the [Releases](https://github.com/avwohl/mpm2/releases) page.

### Debian/Ubuntu (.deb)

```bash
# Download and install
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2-emu-0.3.7-Linux.deb
sudo dpkg -i mpm2-emu-0.3.7-Linux.deb
sudo apt-get install -f  # Install dependencies if needed

# Download disk image
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2_system.img

# Run
mpm2_emu -l -d A:mpm2_system.img
```

### Fedora/RHEL (.rpm)

```bash
# Download and install
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2-emu-0.3.7-Linux.rpm
sudo dnf install ./mpm2-emu-0.3.7-Linux.rpm

# Download disk image
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2_system.img

# Run
mpm2_emu -l -d A:mpm2_system.img
```

### What's in the Packages

| Package | Contents |
|---------|----------|
| `.deb` / `.rpm` | `mpm2_emu` emulator binary |
| `mpm2_system.img` | Pre-built 8MB disk image with MP/M II and utilities |

The disk image is required - it contains the MP/M II operating system, boot loader, and standard utilities.

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
map. The [CHANGELOG](CHANGELOG.md) has the details.

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
too. See [docs/mpm2_v21.md](docs/mpm2_v21.md) for every change between the
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

Until 0.3.6 this README said that DRI's GENSYS relocates an SPR wrongly when
its length is a multiple of 128 bytes. It does not; the fault was in a
GENSYS.COM built from source with an older um80. See
[docs/ldrlwr_bug.md](docs/ldrlwr_bug.md).

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
| SFTP | `asm/SFTP.RSP`, `SFTP.BRS` | the emulator's [SFTP](#sftp-file-transfer) and [HTTP](#http-file-browser) file access |

The first four come from `bin/dri` or `bin/src`, as selected with `--tree`;
the SFTP RSP is the emulator's own, built from `asm/`.  There is no
printer: the XIOS discards list output, so a spooled file goes nowhere
(with `[D]` the spooler still deletes it once it has been listed).

### SSH Setup

Generate host key and configure user authentication:

```bash
mkdir -p keys

# Generate host key (required for SSH)
ssh-keygen -t rsa -b 2048 -m PEM -f keys/ssh_host_rsa_key -N ''
```

**Authentication options:**

| Mode | Configuration | Use Case |
|------|--------------|----------|
| Public key | `keys/authorized_keys` | Production - add user public keys |
| Open access | `--no-auth` flag | Development - accept any connection |

For public key authentication, add authorized public keys:
```bash
# Add your key
cat ~/.ssh/id_rsa.pub >> keys/authorized_keys

# Or multiple users
cat user1.pub user2.pub >> keys/authorized_keys
```

For development/testing without authentication:
```bash
./build/mpm2_emu --no-auth -d A:disks/mpm2_system.img
```

## Running

```bash
./build/mpm2_emu [options] -d A:diskimage

Options:
  -d, --disk A:FILE           Mount disk image (required)
  -l, --local                 Local console mode (output to stdout)
  -w, --http [IP:]PORT        HTTP server address (default: 8000, 0 to disable)
                              Can be repeated for multiple listeners
  --log FILE                  Access log file (default: mpm2.log)
  -p, --port [IP:]PORT        SSH listen address (default: 2222)
                              Can be repeated for multiple listeners
                              Use [IPv6]:PORT for IPv6 addresses
  -k, --key FILE              Host key file (default: keys/ssh_host_rsa_key)
  -a, --authorized-keys FILE  Authorized keys file (default: keys/authorized_keys)
  -n, --no-auth               Disable SSH authentication (accept any connection)
  -t, --timeout SECS          Timeout for debugging
  -h, --help                  Show help
```

The emulator boots from sector 0 of the disk mounted as drive A.

### Examples

```bash
# Local console - see output directly
./build/mpm2_emu -l -d A:disks/mpm2_system.img

# SSH mode - connect via ssh (requires keys/authorized_keys)
./build/mpm2_emu -d A:disks/mpm2_system.img
ssh -p 2222 user@localhost

# SSH mode without authentication (development only)
./build/mpm2_emu --no-auth -d A:disks/mpm2_system.img

# Bind to specific IP address
./build/mpm2_emu -p 127.0.0.1:2222 -w 127.0.0.1:8000 -d A:disks/mpm2_system.img

# Multiple listeners (IPv4 and IPv6)
./build/mpm2_emu -p 127.0.0.1:2222 -p '[::1]:2222' -w 8000 -d A:disks/mpm2_system.img
```

## SFTP File Transfer

The emulator includes an integrated SFTP server for transferring files to and from the MP/M II disk. This allows you to use standard SFTP clients to upload, download, and manage files.

### Connecting

```bash
# Connect with sftp (same port as SSH terminal)
sftp -P 2222 user@localhost
```

### Path Format

SFTP paths use the format `/<drive>.<user>/<filename>`:

| Path | Description |
|------|-------------|
| `/A.0/` | Drive A, user 0 |
| `/B.3/TEST.COM` | Drive B, user 3, file TEST.COM |
| `/A.0/*.TXT` | Wildcard pattern for .TXT files |

### Supported Commands

| Command | Description |
|---------|-------------|
| `ls /A.0/` | List directory |
| `get /A.0/FILE.TXT` | Download file |
| `put local.txt /A.0/FILE.TXT` | Upload file |
| `rm /A.0/FILE.TXT` | Delete file |
| `rename /A.0/OLD.TXT /A.0/NEW.TXT` | Rename file |

### Example Session

```bash
sftp -P 2222 user@localhost
sftp> ls /A.0/
/A.0/GENHEX.COM     /A.0/LIB.COM     /A.0/LINK.COM
sftp> put myfile.txt /A.0/MYFILE.TXT
Uploading myfile.txt to /A.0/MYFILE.TXT
sftp> ls /A.0/MYFILE.TXT
/A.0/MYFILE.TXT
sftp> quit
```

### How It Works

SFTP operations are handled by an RSP (Resident System Process) running inside MP/M II. The C++ emulator receives SFTP protocol messages and forwards them to the Z80 RSP via a bridge interface. The RSP performs actual file operations using BDOS calls, ensuring proper file locking and consistency with MP/M II processes.

- The RSP takes one request at a time, and each stands on its own: a read
  or write opens the file, reads or writes up to 1920 bytes at the request's
  offset and closes it again, and nothing is left open in between.  SFTP
  sessions and HTTP clients can use it at the same time.
- A download reads the whole file through the RSP when the client opens it.
  The RSP opens a file it reads in read-only mode, as TYPE and PIP do, so a
  console program reading the same file is not refused.  A file a console
  program has open in locked mode (ED's file, say) cannot be read until it
  is closed: SFTP reports it missing, HTTP answers 404.
- The RSP runs in the BDOS's return-error mode: an error comes back to it
  as a status and is never printed on a console.
- An upload creates the file when the client opens it (`put` replaces one of
  the same name), and holds what the client writes until the client closes
  it, when it is written.  `put` returns only once it is all on the disk and
  closed, so a console can use the file straight away.
- CP/M files are whole 128-byte records.  An upload whose length is not a
  multiple of 128 is padded with ctl-Z (1AH), and a download returns whole
  records, padding included.

Files involved:
- `asm/sftp_brs.plm` - Z80 RSP code (PL/M-80)
- `asm/sftp_glue.asm` - Assembly glue for BDOS calls, taking its arguments
  as PL/M-80 passes them (BC, DE)
- `src/sftp_bridge.cpp` - C++ request/reply bridge
- `src/ssh_session_libssh.cpp` - SFTP protocol handling

## HTTP File Browser

The emulator includes a read-only HTTP server for browsing and downloading files from MP/M II disks using a web browser.

### Accessing

Open in any web browser:
```
http://localhost:8000/
```

### Path Format

| Path | Description |
|------|-------------|
| `/` | List mounted drives |
| `/a/` | Drive A - the listing is of user 0, as `/a.0/` |
| `/a.0/` | Drive A, user 0 only |
| `/a/file.txt` | Download file from drive A |
| `/a.0/file.txt` | Download file from drive A, user 0 |

- URLs are case-insensitive (`/A/FILE.TXT` and `/a/file.txt` both work)
- A file path without a user number (`/a/file.txt`) reads user 0
- Directory listings show filenames in lowercase
- Text files (.txt, .asm, .plm, etc.) are served with Unix line endings (CR stripped), ending at the first ctl-Z
- Other files are served as whole 128-byte records

### Configuration

```bash
# Default: HTTP on port 8000
./build/mpm2_emu -d A:disks/mpm2_system.img

# Custom port
./build/mpm2_emu -w 8080 -d A:disks/mpm2_system.img

# Disable HTTP server
./build/mpm2_emu -w 0 -d A:disks/mpm2_system.img
```

### How It Works

HTTP file operations share the same RSP bridge as SFTP. When an HTTP request arrives, it queues file requests to the Z80 RSP, which performs the actual disk reads via BDOS calls. Requests from HTTP and SFTP clients are served one at a time, and each opens and closes its file, so they can be interleaved safely.

## Testing

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
[Building from Source](#building-from-source)).

`python3 tools/verify_dri.py` builds the nucleus, BNKBDOS, `ABORT.RSP`,
`DUMP.PRL`, `GENHEX.COM`, `GENMOD.COM`, the assembler, the debugger and the
loader of both releases with `--dri-exact` and compares them with Digital
Research's binaries.

## Access Logging

The emulator logs HTTP, SSH, and SFTP access to a file (default: `mpm2.log`):

```
2026-01-06 23:21:19 [HTTP] 127.0.0.1 GET /
2026-01-06 23:21:26 [SSH] 127.0.0.1 connected
2026-01-06 23:21:26 [SSH] 127.0.0.1 auth user=test method=none
2026-01-06 23:21:26 [SSH] 127.0.0.1 exec command=exit
2026-01-06 23:21:29 [SSH] 127.0.0.1 disconnected
```

Each log entry includes:
- ISO timestamp (YYYY-MM-DD HH:MM:SS)
- Service type (HTTP, SSH, SFTP)
- Client IP address
- Event details (request path, auth method, command, etc.)

To use a different log file:
```bash
./build/mpm2_emu --log /var/log/mpm2.log -d A:disks/mpm2_system.img
```

## Project Structure

```
mpm2/
├── scripts/
│   ├── build_all.sh      # Master build script (--tree, --version, ...)
│   ├── build_src.sh      # Build from source code into bin/src/
│   ├── build_hd1k.sh     # Create disk image with MP/M II files
│   ├── build_sftp_rsp.sh # Build the SFTP RSP (SFTP.RSP, SFTP.BRS)
│   ├── build_asm.sh      # Assemble Z80 code, build C++, write boot sector
│   ├── gensys.sh         # Generate MPM.SYS, with the RSPs
│   ├── run_tests.sh      # Test runner (basic, stat, rsp, http, sftp, all, src)
│   ├── test_ssh.exp      # Run console commands over SSH
│   ├── test_rsp.exp      # Resident system process checks
│   └── test_sftp.exp     # SFTP put/get, HTTP read, TYPE and SUBMIT
├── bin/
│   ├── dri/              # Original DRI binaries (.COM, .PRL, .SPR)
│   └── src/              # Source-built binaries (generated)
├── src/
│   ├── overrides/        # Source code modifications, V2.1 recovery and
│   │   │                 # local fixes, by DRI source directory
│   │   ├── MPMLDR/       # MPMLDR's serial check off (not --dri-exact), GENSYS V2.1
│   │   ├── NUCLEUS/      # Kernel source overrides
│   │   └── UTIL2, UTIL4..UTIL7/ # RSPs and transients
│   ├── mpm_pagezero.mac  # uplm80's own page-zero symbols (??BDOS, ??BOOT,
│   │                     # ??MAXB), linked with DRI's PLM_WORK/X0100.ASM
│   ├── brs_runtime.mac   # uplm80's ??BDOS and ??BOOT for a banked RSP,
│   │                     # linked with DRI's UTIL2/BRSPBI.ASM
│   ├── main.cpp          # Emulator entry point and main loop
│   ├── http_server.cpp   # HTTP file browser
│   ├── sftp_bridge.cpp   # SFTP/HTTP to Z80 bridge
│   └── ssh_session_libssh.cpp # SSH/SFTP server
├── tools/
│   ├── build.py          # Source build script (Python)
│   ├── genmod.py         # GENMOD, GENHEX, PRLCOM and LOAD, for ASM, RDT, DDT, GENHEX, GENMOD, MPMLDR
│   ├── gensys.py         # MP/M II system generator (replaces DRI GENSYS)
│   ├── verify_dri.py     # Compare a --dri-exact build with DRI's binaries
│   ├── v21/              # Tools the V2.1 reconstruction was done with
│   └── dri_patch.py      # Binary patching tool
├── asm/
│   ├── coldboot.asm      # Boot sector (loads MPMLDR + LDRBIOS)
│   ├── ldrbios.asm       # Loader BIOS for boot phase
│   ├── bnkxios.asm       # Runtime XIOS (I/O port dispatch)
│   ├── sftp_rsp.plm      # SFTP RSP process descriptor (common memory)
│   ├── sftp_brs.plm      # SFTP RSP banked code (PL/M-80)
│   ├── sftp_glue.asm     # SFTP assembly glue for BDOS calls (BC, DE in)
│   └── sftp_brs_header.asm # SFTP RSP header and entry point
├── include/              # C++ headers
│   └── logger.h          # Access logging
├── docs/                 # Write-ups; mpm2_v21.md is the V2.1 reconstruction
├── build/                # CMake build, source build and test logs (generated)
├── disks/                # Disk images (generated)
└── mpm2_external/        # MP/M II source and distribution
    ├── mpm2src/          # Original source code (V2.0); CONTROL is the V2.0 master
    └── mpm2dist/         # Original binaries (V2.1)
```

## How It Works

MP/M II is Digital Research's multi-user, multi-tasking operating system for Z80. This emulator:

1. Boots from disk sector 0 (cold boot loader)
2. Loads MPMLDR and LDRBIOS from reserved tracks
3. MPMLDR loads MPM.SYS into high memory
4. Provides 7 memory banks (48KB user + 16KB common each)
5. Runs 60Hz timer interrupts for task switching
6. Exposes 4 consoles via SSH connections

The XIOS uses I/O port traps - Z80 code does `OUT (0xE0), A` and the emulator intercepts to handle disk, console, and system functions.

## Troubleshooting

### "MPM SYS ?" error on boot
Run `./scripts/gensys.sh` to regenerate MPM.SYS with matching serial numbers.

### Build fails with "um80 not found"
Install um80/ul80: `pip install -e path/to/um80_and_friends`

### "dyld: Library not loaded: /usr/local/lib/libqkz80.4.dylib" (macOS)
An emulator linked before 0.3.6 took cpmemu's `libqkz80` dylib, which is only
at that path after cpmemu's `make install`. The build now links `libqkz80.a`
by path; remove `build/` and run `./scripts/build_all.sh` again.

### SSH connection refused
Ensure the emulator is running and check if port 2222 is available.

### No output after boot
Use `-l` flag for local console mode to see boot messages.

## License

GPL-3.0-or-later

## References

- [MP/M II Command Reference](https://github.com/avwohl/retro_docs/blob/main/mpm2/mpm2_summary.pdf) - Quick reference for all commands
- [MP/M II System Guide](mpm2_external/docs/) - Original Digital Research documentation
- [RomWBW](https://github.com/wwarthen/RomWBW) - hd1k disk format
- [cpmemu](https://github.com/avwohl/cpmemu) - Z80 emulator with cpm_disk.py utility
## Related Projects

- [80un](https://github.com/avwohl/80un) - Unpacker for the CP/M archive and compression formats LBR, ARC, squeeze, crunch, and CrLZH.
- [cpmdroid](https://github.com/avwohl/cpmdroid) - Z80/CP/M emulator for Android phones and tablets. It emulates the RomWBW HBIOS interface and a VT100 terminal.
- [cpmemu](https://github.com/avwohl/cpmemu) - Z80/CP/M emulator for Linux and Windows, with Z80 and 8080 CPU cores. It translates the BDOS and BIOS calls of CP/M 2.2 programs to the host file system.
- [ioscpm](https://github.com/avwohl/ioscpm) - Z80/CP/M emulator for iOS and macOS. It emulates the RomWBW HBIOS interface and runs CP/M 2.2 and CP/M 3.
- [learn-ada-z80](https://github.com/avwohl/learn-ada-z80) - Collection of more than 90 Ada example programs for uada80, the Ada compiler for the Z80 processor and CP/M.
- [mbasic](https://github.com/avwohl/mbasic) - Python interpreter for MBASIC 5.21, the Microsoft BASIC-80 for CP/M. Two compiler backends compile the programs to CP/M .COM files or to JavaScript.
- [mbasic2025](https://github.com/avwohl/mbasic2025) - Reconstruction of the lost source code of MBASIC 5.21, the Microsoft BASIC-80 for CP/M. The MACRO-80 source code assembles to a binary that matches mbasic.com byte for byte.
- [mbasicc](https://github.com/avwohl/mbasicc) - C++17 interpreter for MBASIC 5.21, the Microsoft BASIC-80 for CP/M. It runs on Linux and macOS.
- [mbasicc_web](https://github.com/avwohl/mbasicc_web) - Web browser interpreter for MBASIC 5.21, the Microsoft BASIC-80 for CP/M. Emscripten compiles the mbasicc interpreter to WebAssembly.
- [romwbw_emu](https://github.com/avwohl/romwbw_emu) - Hardware-level Z80/CP/M emulator for Linux and macOS. It emulates the RomWBW HBIOS interface and switches banks in 512 KB of ROM and 512 KB of RAM.
- [scelbal](https://github.com/avwohl/scelbal) - Floating-point BASIC interpreter for the 8080 processor and CP/M. A translator converts the original 8008 source code to 8080 source code.
- [uada80](https://github.com/avwohl/uada80) - Ada compiler for the Z80 processor and CP/M 2.2. It compiles a subset of Ada 2012 to CP/M .COM files.
- [uc80](https://github.com/avwohl/uc80) - C compiler for the Z80 processor and CP/M. It optimizes for small code size.
- [ucow](https://github.com/avwohl/ucow) - Cowgol compiler for the Z80 processor and CP/M. It runs on Linux in Python.
- [um80_and_friends](https://github.com/avwohl/um80_and_friends) - Linux toolchain that is compatible with Microsoft MACRO-80. It has an assembler, a linker, a librarian, and a disassembler.
- [upeepz80](https://github.com/avwohl/upeepz80) - Peephole optimizer for Z80 compilers that write lowercase Z80 assembly language. It shortens jumps to jr, builds djnz loops, and removes dead stores.
- [uplm80](https://github.com/avwohl/uplm80) - PL/M-80 compiler for the Z80 processor and CP/M. It writes Intel 8080 and Zilog Z80 assembly language.
- [z80cpmw](https://github.com/avwohl/z80cpmw) - Z80/CP/M emulator for Windows. It emulates the RomWBW HBIOS interface and boots CP/M from disk images.

