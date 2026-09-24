# MP/M II Emulator

A Z80-based MP/M II emulator with SSH terminal access. Multiple users can connect simultaneously to run CP/M-compatible software.

**[MP/M II Command Reference](https://github.com/avwohl/retro_docs/blob/main/mpm2/mpm2_summary.pdf)** - Complete guide to all commands and utilities

## Quick Start

```bash
# Build with DRI binaries (default, fast)
./scripts/build_all.sh

# Or build from source (requires uplm80/um80/ul80)
./scripts/build_all.sh --tree=src

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
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2-emu-0.3.4-Linux.deb
sudo dpkg -i mpm2-emu-0.3.4-Linux.deb
sudo apt-get install -f  # Install dependencies if needed

# Download disk image
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2_system.img

# Run
mpm2_emu -l -d A:mpm2_system.img
```

### Fedora/RHEL (.rpm)

```bash
# Download and install
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2-emu-0.3.4-Linux.rpm
sudo dnf install ./mpm2-emu-0.3.4-Linux.rpm

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
| Python 3 | Build scripts, um80/ul80 | Usually pre-installed |
| cpmemu | Z80 CPU emulator + disk tools | Clone from github.com/avwohl/cpmemu |

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

# uplm80 - PL/M-80 cross-compiler (required for --tree=src builds)
git clone https://github.com/avwohl/uplm80.git
cd uplm80
pip install -e .  # Installs uplm80 command
cd ..

# MP/M II distribution files are included in the mpm2_external/ directory
```

um80 and uplm80 are also on PyPI (`pip install um80 uplm80`). A source build
needs um80 0.3.48 or later and uplm80 0.3.6 or later, which brings upeepz80
0.2.4: earlier releases link `.PRL` transients a page low and miscompile
several of the utilities.

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
| `dri` | Original DRI binaries (default) | None - binaries included |
| `src` | Build from source code | uplm80, um80, ul80 |

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
check disabled in `src/overrides/MPMLDR/MPMLDR.PLM`.

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
byte identical to Digital Research's own V2.0 and V2.1 binaries. See
[docs/mpm2_v21.md](docs/mpm2_v21.md) for what changed between the releases
and what is still outstanding.

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
| `--serial=none\|dri` | Serial number in a source-built nucleus: the sources' `654321` placeholder (default) or the one on DRI's master; `--tree=src` only |
| `--dri-exact` | Build what DRI shipped: DRI's serial number, without the local fixes `src/overrides` keeps behind `DRIEXACT`; `--tree=src` only |

### Modern GENSYS

The original DRI GENSYS.COM has a bug in its relocation code (LDRLWR.ASM) that
corrupts SPR/BRS files when code size doesn't align well with 128-byte sectors.
The bug is most severe at exactly 1024 bytes where 100% of relocation uses garbage.

**The Bug:** LDRLWR.ASM loads `ceil(prgsiz/128)` sectors, which includes code plus
extra bytes from rounding. It uses these extra bytes as the relocation bitmap. When
more bitmap is needed, it should read from disk - but the detection check compares
the bitmap pointer against an unrelated buffer address (`bitmap+128`) instead of
checking if it exceeded the loaded data. Result: garbage is used instead of the
actual bitmap.

This project uses a Python replacement (`tools/gensys.py`) that reads the complete
bitmap directly from the SPR file and applies it correctly:

- Fixes the bitmap relocation bug for all file sizes
- Reads configuration from JSON instead of interactive prompts
- Generates identical MPM.SYS output for valid inputs
- Supports RSP modules with banked code (BRS files)

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
- `asm/sftp_glue.asm` - Assembly glue for BDOS calls
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
| `src` | `build_all.sh --tree=src`, then `basic`, `rsp`, `http` and `sftp` |
| `interactive` | an SSH session to type at |

The SSH port is `PORT` (default 2222) and the HTTP port `HTTP_PORT` (default
`PORT` + 6000), so two checkouts can run their tests at once on different
ports.  Logs go to `build/mpm2_test.log` and `build/mpm2_src_build.log`.

`python3 tools/verify_dri.py` builds the nucleus of both releases with
`--dri-exact` and compares it with Digital Research's binaries.

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
│   ├── build_all.sh      # Master build script (--tree=dri|src)
│   ├── build_src.sh      # Build from source code
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
│   │   │                 # compiler workarounds, by DRI source directory
│   │   ├── BNKBDOS/      # Banked BDOS
│   │   ├── MPMLDR/       # MPMLDR with disabled serial check, GENSYS V2.1
│   │   ├── NUCLEUS/      # Kernel source overrides
│   │   └── UTIL2, UTIL4..UTIL7/ # RSPs and transients
│   └── cpm_runtime.mac   # Runtime support for PL/M programs
├── tools/
│   ├── build.py          # Source build script (Python)
│   ├── gensys.py         # MP/M II system generator (replaces DRI GENSYS)
│   └── dri_patch.py      # Binary patching tool
├── asm/
│   ├── coldboot.asm      # Boot sector (loads MPMLDR + LDRBIOS)
│   ├── ldrbios.asm       # Loader BIOS for boot phase
│   ├── bnkxios.asm       # Runtime XIOS (I/O port dispatch)
│   ├── sftp_brs.plm      # SFTP RSP banked code (PL/M-80)
│   ├── sftp_glue.asm     # SFTP assembly glue for BDOS calls
│   └── sftp_brs_header.asm # SFTP RSP header and entry point
├── src/                  # C++ emulator source
│   ├── main.cpp          # Entry point and main loop
│   ├── http_server.cpp   # HTTP file browser
│   ├── sftp_bridge.cpp   # SFTP/HTTP to Z80 bridge
│   └── ssh_session_libssh.cpp # SSH/SFTP server
├── include/              # C++ headers
│   ├── logger.h          # Access logging
├── build/                # CMake build directory (generated)
├── disks/                # Disk images (generated)
└── mpm2_external/        # MP/M II source and distribution
    ├── mpm2src/          # Original source code
    └── mpm2dist/         # Original binaries
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

