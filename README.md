# MP/M II Emulator

A Z80-based MP/M II emulator with SSH terminal access. Multiple users can connect simultaneously to run CP/M-compatible software.

**[MP/M II Command Reference](docs/mpm2_summary.pdf)** - Complete guide to all commands and utilities

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
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2-emu_0.3.0_amd64.deb
sudo dpkg -i mpm2-emu_0.3.0_amd64.deb
sudo apt-get install -f  # Install dependencies if needed

# Download disk image
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2_system.img

# Run
mpm2_emu -l -d A:mpm2_system.img
```

### Fedora/RHEL (.rpm)

```bash
# Download and install
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2-emu-0.3.0-1.x86_64.rpm
sudo dnf install ./mpm2-emu-0.3.0-1.x86_64.rpm

# Download disk image
wget https://github.com/avwohl/mpm2/releases/latest/download/mpm2_system.img

# Run
mpm2_emu -l -d A:mpm2_system.img
```

### What's in the Packages

| Package | Contents |
|---------|----------|
| `.deb` / `.rpm` | `mpm2_emu` emulator binary, `mkboot` utility |
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

# MP/M II distribution files (required) - contact maintainer for access
# Should be placed in mpm2/mpm2_external/
```

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
3. **build_asm.sh** - Assembles LDRBIOS and BNKXIOS, builds C++ emulator, writes boot sector
4. **gensys.sh** - Runs GENSYS to create MPM.SYS (4 consoles, 7 memory banks)

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
  -w, --http PORT             HTTP server port (default: 8000, 0 to disable)
  --log FILE                  Access log file (default: mpm2.log)
  -p, --port PORT             SSH listen port (default: 2222)
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

# Custom SSH port
./build/mpm2_emu -p 2223 -d A:disks/mpm2_system.img
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
| `/a/` | Drive A, all users |
| `/a.0/` | Drive A, user 0 only |
| `/a/file.txt` | Download file from drive A |
| `/a.0/file.txt` | Download file from drive A, user 0 |

- URLs are case-insensitive (`/A/FILE.TXT` and `/a/file.txt` both work)
- Directory listings show filenames in lowercase
- Text files (.txt, .asm, .plm, etc.) are served with Unix line endings (CR stripped)

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

HTTP file operations share the same RSP bridge as SFTP. When an HTTP request arrives, it queues a file request to the Z80 RSP, which performs the actual disk read via BDOS calls. Requests from HTTP and SFTP clients are serialized to ensure consistent access.

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
│   ├── build_asm.sh      # Assemble Z80 code, build C++, write boot sector
│   └── gensys.sh         # Generate MPM.SYS
├── bin/
│   ├── dri/              # Original DRI binaries (.COM, .PRL, .SPR)
│   └── src/              # Source-built binaries (generated)
├── src/
│   ├── overrides/        # Source code modifications
│   │   ├── MPMLDR/       # MPMLDR with disabled serial check
│   │   └── NUCLEUS/      # Kernel source overrides
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
└── mpm2_external/        # MP/M II source and distribution (not in git)
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

- [MP/M II Command Reference](docs/mpm2_summary.pdf) - Quick reference for all commands
- [MP/M II System Guide](mpm2_external/docs/) - Original Digital Research documentation
- [RomWBW](https://github.com/wwarthen/RomWBW) - hd1k disk format
- [cpmemu](https://github.com/avwohl/cpmemu) - Z80 emulator with cpm_disk.py utility
