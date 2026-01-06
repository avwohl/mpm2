# MP/M II Emulator

MP/M II (Multi-Programming Monitor for CP/M) emulator with SSH terminal access.

## Project Overview

This emulator runs MP/M II, Digital Research's multi-user, multi-tasking operating system for Z80-based computers. The primary use case is testing the uada80 Ada compiler which requires multi-user/multi-tasking capabilities.

## Architecture

### Execution Model

The emulator runs single-threaded with polling:
- **Main Loop**: Runs Z80 CPU in batches, handles console I/O, 60Hz timer interrupts
- **SSH Sessions**: Bridge SSH connections to console I/O queues

### Memory Model

MP/M II uses bank-switched memory:
- **Lower 48KB (0x0000-0xBFFF)**: Banked memory, switchable per process
- **Upper 16KB (0xC000-0xFFFF)**: Common memory, shared by all processes

### Key Components

| Component | Files | Purpose |
|-----------|-------|---------|
| Console | `console.h/cpp`, `console_queue.h` | Thread-safe character queues for terminal I/O |
| Memory | `banked_mem.h/cpp` | Bank-switched memory for process isolation |
| XIOS | `xios.h/cpp` | Extended I/O System - MP/M II hardware abstraction |
| Z80 | `z80_runner.h/cpp` | CPU emulation with timer interrupts |
| Disk | `disk.h/cpp` | CP/M-compatible disk image I/O |
| SSH | `ssh_session.h/cpp` | libssh-based terminal access |

## XIOS Entry Points

The XIOS (Extended I/O System) provides MP/M II's hardware abstraction. Entry points at `XIOS_BASE + offset`:

| Offset | Function | Purpose |
|--------|----------|---------|
| 0x00-0x30 | Standard BIOS | WBOOT, CONST, CONIN, CONOUT, etc. |
| 0x33 | SELMEMORY | Select memory bank (reg C = bank number) |
| 0x36 | POLLDEVICE | Poll device for ready (reg C = device) |
| 0x39 | STARTCLOCK | Enable tick interrupts |
| 0x3C | STOPCLOCK | Disable tick interrupts |
| 0x3F | EXITREGION | Exit mutual exclusion region |
| 0x42 | MAXCONSOLE | Return max console number in A |
| 0x45 | SYSTEMINIT | System initialization |

## Development Guidelines

### SFTP File I/O - RSP Bridge Only

**CRITICAL: All SFTP file operations (directory listing, file read, file write, stat, etc.) MUST go through the Z80 RSP bridge. DO NOT implement direct C++ disk access for SFTP.**

The SFTP RSP runs as a Resident System Process in MP/M II and handles file operations via BDOS calls. This ensures:
- Proper file locking semantics
- Consistent view with MP/M II processes
- User area handling

Files involved:
- `asm/sftp_brs.plm` - Z80 RSP code (PL/M)
- `src/sftp_bridge.cpp` - C++ request/reply queue
- `src/xios.cpp` - XIOS dispatch for SFTP (0x60-0x69)

The only direct disk access allowed is `get_mounted_drives()` to list available drives.

### Loading Binary Data

**Always load binary data from assembled files rather than manually constructing byte sequences in code.** This applies to:
- Disk parameter blocks (DPB) - use DISKDEF.LIB macros in assembly
- BIOS/XIOS jump tables - assemble from source
- Boot images - use mkboot tool with assembled binaries

Manual byte poking leads to subtle errors. Use the tested DISKDEF.LIB macros from the MP/M II distribution for disk parameters.

### Disk Formats

**IMPORTANT: Use `../cpmemu/util/cpm_disk.py` for all disk image operations. DO NOT use cpmtools (cpmls, cpmcp, mkfs.cpm) or diskdefs files.**

```bash
# List files on disk
../cpmemu/util/cpm_disk.py list disks/mpm2_system.img

# Add file to disk
../cpmemu/util/cpm_disk.py add disks/mpm2_system.img localfile.com

# Extract file from disk
../cpmemu/util/cpm_disk.py extract disks/mpm2_system.img FILENAME.COM

# Delete file
../cpmemu/util/cpm_disk.py delete disks/mpm2_system.img FILENAME.COM

# Create new hd1k disk
../cpmemu/util/cpm_disk.py create disks/newdisk.img
```

The emulator uses disk images with **NO SECTOR SKEW** for simplicity. The hd1k format (8MB hard disk) is the primary supported format:

| Format | Size | Geometry |
|--------|------|----------|
| hd1k | 8MB | 1024 trk, 16 sec, 512 bytes, 4K blocks |

### LDRBIOS

|------|--------|-----|----------|
| ldrbios.asm | 8" SSSD | SPT=26, BSH=3, EXM=0 | Floppy boot |

The build script (`build_asm.sh`) selects the correct LDRBIOS based on the target format.

**Critical**: The LDRBIOS DPH must match the diskdef used to create the image:
- LDRBIOS has its own DPB embedded in the binary
- MPMLDR reads this DPB to calculate sector/track for file reads
- Mismatch = boot failure

## Building

### Dependencies

- CMake 3.16+
- C++17 compiler
- libssh (optional, for SSH support): `brew install libssh`

### Build Commands

Full build (assembles Z80 code, builds C++, creates disk, generates MPM.SYS):
```bash
./scripts/build_all.sh
```

Or manual steps:
```bash
mkdir build && cd build
cmake ..
make
```

For SSH support, install libssh:
```bash
# macOS
brew install libssh

# Linux (Debian/Ubuntu)
sudo apt install libssh-dev

# Build mpm2 with SSH support
cd mpm2/build
cmake ..
make
```

### SSH Setup

```bash
mkdir -p keys

# Generate host key (required)
ssh-keygen -t rsa -b 2048 -m PEM -f keys/ssh_host_rsa_key -N ''

# Copy your public key for authentication
cp ~/.ssh/id_rsa.pub keys/authorized_keys
```

## Usage

```bash
./mpm2_emu [options] -d A:diskimage

Options:
  -d, --disk A:FILE           Mount disk image on drive A-P (required)
  -l, --local                 Enable local console (output to stdout)
  -t, --timeout SECS          Timeout in seconds for debugging
  -w, --http PORT             HTTP server port (default: 8000, 0 to disable)
  -p, --port PORT             SSH listen port (default: 2222)
  -k, --key FILE              Host key file (default: keys/ssh_host_rsa_key)
  -a, --authorized-keys FILE  Authorized keys file (default: keys/authorized_keys)
  -n, --no-auth               Disable SSH authentication (accept any connection)
  -h, --help                  Show help

Examples:
  # Local console mode
  ./mpm2_emu -l -d A:disks/mpm2_system.img

  # SSH mode (requires keys/authorized_keys)
  ./mpm2_emu -d A:disks/mpm2_system.img

  # SSH mode without authentication (development only)
  ./mpm2_emu --no-auth -d A:disks/mpm2_system.img
```

The emulator boots from disk sector 0 of drive A using the cold start loader.

### HTTP File Browser

The HTTP server (default port 8000) provides read-only web access to MP/M II files:

- `http://localhost:8000/` - List mounted drives
- `http://localhost:8000/a/` - List all users on drive A
- `http://localhost:8000/a.0/` - List user 0 only on drive A
- `http://localhost:8000/a/file.txt` - Download file

URLs are case-insensitive. Directory listings show lowercase filenames.
Text files are served with Unix line endings (CR stripped, 0x1A EOF removed).

## Testing SSH

Use the test harness script to verify SSH input and output work correctly:

```bash
./scripts/test_ssh.sh
```

This script:
1. Starts the emulator on a test port (2223)
2. Connects via SSH using expect
3. Waits for the MP/M II banner and prompt
4. Sends a `stat` command (not `dir` since `dir` runs automatically from startup batch)
5. Verifies response contains disk space info
6. Reports pass/fail status

**Expected output:**
```
=== SSH Test Harness ===
...
>>> OUTPUT TEST: Got MP/M II banner
>>> OUTPUT TEST: Got command prompt
>>> INPUT TEST: Sending 'stat' command
>>> INPUT TEST: Got stat response showing disk space
>>> SUCCESS: Both input and output working!
=== TEST PASSED ===
```

**Common issues:**
- "Serial numbers do not match": Run `./scripts/gensys.sh` to rebuild the disk image
- No output after banner: Disk image may need regeneration
- Connection refused: Check another emulator isn't running on that port

## Boot Process - The Four Layers

MP/M II boot involves four distinct software layers. **Each layer uses different BIOS code.**

```
┌─────────────────────────────────────────────────────────────────┐
│ Layer 1: Cold Start Loader (sectors 0-1 of boot disk)          │
│          - Loaded by hardware/ROM into memory                  │
│          - Loads MPMLDR from system tracks into 0x0100         │
│          - Very small (~128 bytes)                             │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│ Layer 2: MPMLDR (0x0100-0x16FF)                                 │
│          - Contains LDRBDOS (loader BDOS)                       │
│          - Prints: "MP/M II V2.1 Loader"                        │
│          - Searches directory for MPM.SYS                       │
│          - Loads MPM.SYS into high memory                       │
│          - Uses LDRBIOS for all disk/console I/O                │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│ Layer 3: LDRBIOS (0x1700+)                                      │
│          - CP/M 2-compatible BIOS for the loader phase          │
│          - Provides: SELDSK, SETTRK, SETSEC, SETDMA, READ       │
│          - Provides: CONOUT for loader messages                 │
│          - Has its own DPB matching the boot disk format        │
│          - Files: ldrbios.asm (SSSD), ldrbios_hd1k.asm (hd1k)   │
│          - ONLY used during boot, then discarded                │
└─────────────────────────────────────────────────────────────────┘
                              ↓
┌─────────────────────────────────────────────────────────────────┐
│ Layer 4: MPM.SYS / BNKXIOS (loaded by MPMLDR)                   │
│          - Full MP/M II operating system                        │
│          - BNKXIOS provides runtime XIOS (extended BIOS)        │
│          - LDRBIOS is NO LONGER USED after this point           │
│          - Handles multiple consoles, bank switching, etc.      │
│          - File: bnkxios_port.asm → BNKXIOS.SPR                 │
└─────────────────────────────────────────────────────────────────┘
```

### Key Points

1. **LDRBIOS ≠ BNKXIOS**: The loader BIOS and the runtime XIOS are completely separate code. LDRBIOS is a minimal CP/M 2 BIOS; BNKXIOS is the full MP/M II extended I/O system.

2. **First LDRBIOS call is SELDSK**: MPMLDR's first call to LDRBIOS is SELDSK (select disk). Put any initialization code in SELDSK.

3. **LDRBIOS must match disk format**: The DPB embedded in LDRBIOS must match the format of the boot disk image. Mismatch = boot failure.

4. **LDRBIOS address limit**: Must not extend above the base of MPM.SYS being loaded. For floppy boot, upper limit is 0x1A00.

5. **MPMLDR searches for MPM.SYS**: If MPMLDR prints "MPM SYS ?" error, the file is not on the disk or the directory read failed.

### Boot Sequence Detail

1. Emulator loads boot image into memory (contains MPMLDR + LDRBIOS)
2. Z80 starts executing at 0x0100 (MPMLDR entry)
3. MPMLDR calls LDRBIOS SELDSK to select drive A:
4. MPMLDR reads directory (track 2+) looking for MPM.SYS
5. MPMLDR loads MPM.SYS records into high memory
6. MPMLDR jumps to MPM.SYS execution address
7. MP/M II initializes BNKXIOS and presents console prompts
8. **LDRBIOS is never called again** - all I/O now goes through BNKXIOS

## Disk Image Formats

Disk format is auto-detected based on file size:

| Format | Size | Geometry | Dir Entries |
|--------|------|----------|-------------|
| 8" SSSD | 250KB | 77 trk, 26 sec, 128 bytes | 64 |
| hd1k | 8MB (8,388,608 bytes) | 1024 trk, 16 sec, 512 bytes | 1024 |
| hd512 | 8.32MB (8,519,680 bytes) | 1040 trk, 16 sec, 512 bytes | 512 |

The **hd1k** format (from RomWBW) is recommended for MP/M II due to its larger size and directory capacity.

### Working with Disk Images (cpm_disk.py)

The upstream cpmemu project provides `cpm_disk.py` for disk management. It creates hd1k images without sector skew:

```bash
# Create new disk image
../cpmemu/util/cpm_disk.py create -f disks/mpm2_hd1k.img

# Add files with SYS attribute (IMPORTANT for MP/M II!)
../cpmemu/util/cpm_disk.py add --sys disks/mpm2_hd1k.img mpm2_external/mpm2dist/*.PRL
../cpmemu/util/cpm_disk.py add --sys disks/mpm2_hd1k.img mpm2_external/mpm2dist/*.SPR
../cpmemu/util/cpm_disk.py add disks/mpm2_hd1k.img mpm2_external/mpm2dist/MPM.SYS

# List files
../cpmemu/util/cpm_disk.py list disks/mpm2_hd1k.img
```

### MP/M II Console User Numbers

**IMPORTANT**: In MP/M II, each console starts in a different user area by default:
- Console 0 starts in user 0
- Console 1 starts in user 1
- Console 2 starts in user 2
- Console 3 starts in user 3

The prompt "3A>" means user 3, drive A (NOT console 3).

### Startup Files

MP/M II's TMP (Terminal Message Processor) executes a startup file when each console logs in. The startup file naming convention is:

| Console | User | Startup File |
|---------|------|--------------|
| 0 | 0 | `$0$.SUP` |
| 1 | 1 | `$1$.SUP` |
| 2 | 2 | `$2$.SUP` |
| 3 | 3 | `$3$.SUP` |

**Key points:**
- Filename has TWO dollar signs: `$n$.SUP` (not `$n.SUP`)
- Each startup file must be in the **matching user area** (console n looks in user n)
- Content is CP/M text: commands followed by `\r`, padded with `0x1A` to 128 bytes

The `build_hd1k.sh` script automatically creates startup files containing a `DIR` command for all 4 consoles.

To create startup files manually:
```bash
# Create startup file content
printf 'DIR\r' > /tmp/startup.sup
dd if=/dev/zero bs=1 count=124 | tr '\0' '\032' >> /tmp/startup.sup

# Add to correct user area
../cpmemu/util/cpm_disk.py add -u 3 disks/mpm2_hd1k.img /tmp/startup.sup '$3$.SUP'
```

### File Placement for Multi-User Systems

For files to be found, they must be in the **same user area as the console** or in user 0 with the SYS attribute. However, the SYS attribute search appears to not work reliably in some MP/M II configurations.

**Recommended approach**: Place files in the user area matching the console:
```bash
# For console 3 (user 3)
../cpmemu/util/cpm_disk.py add -u 3 disks/mpm2_hd1k.img mpm2_external/mpm2dist/*.PRL
../cpmemu/util/cpm_disk.py add -u 3 disks/mpm2_hd1k.img mpm2_external/mpm2dist/*.SPR
../cpmemu/util/cpm_disk.py add -u 3 disks/mpm2_hd1k.img /path/to/MPM.SYS
```

### MP/M II Command Search Order

When you type a command, CLI.ASM searches in this order:

1. **Current user, current drive**: Try `.PRL` then `.COM`
2. **User 0, current drive**: Try `.PRL` then `.COM` (only files with SYS attribute)
3. **User 0, system drive**: Try `.PRL` then `.COM` (only SYS files)

### SYS Attribute

The SYS attribute allows files in user 0 to be found from any user area. To set SYS attribute:
```bash
../cpmemu/util/cpm_disk.py add --sys disks/mpm2_hd1k.img mpm2_external/mpm2dist/*.PRL
```

**Technical detail**: The SYS attribute is bit 7 of byte 10 in the directory entry (second character of extension). CP/M extension attribute bits:
- Byte 9 (t1): bit 7 = R/O (Read-Only)
- Byte 10 (t2): bit 7 = SYS (System)
- Byte 11 (t3): bit 7 = Archive

For a `.PRL` file, the 'R' becomes 0xD2 ('R' | 0x80) when SYS is set.

## Building the XIOS

The XIOS and LDRBIOS are written in Z80 assembly and built using um80/ul80:

```bash
# Full build including assembly
./scripts/build_all.sh

# Or just assembly step
./scripts/build_asm.sh
```

The build uses:
- **um80** - MACRO-80 compatible assembler (`.asm` -> `.rel`)
- **ul80** - LINK-80 compatible linker (`.rel` -> `.bin` or `.spr`)

This produces:
- `asm/ldrbios_hd1k.bin` - Loader BIOS for hd1k disks (loads at 0x1700)
- `asm/BNKXIOS_port.SPR` - Banked XIOS as relocatable SPR file

## File Organization

```
mpm2/
├── CMakeLists.txt
├── CLAUDE.md
├── README.md
├── asm/
│   ├── ldrbios.asm       # Loader BIOS for 8" SSSD (floppy)
│   ├── bnkxios.asm       # Banked XIOS (I/O port dispatch)
│   └── *.bin, *.SPR      # Generated (in .gitignore)
├── include/
│   ├── banked_mem.h
│   ├── console.h
│   ├── console_queue.h
│   ├── disk.h
│   ├── http_server.h      # HTTP file browser
│   ├── sftp_bridge.h      # SFTP/HTTP to Z80 bridge
│   ├── sftp_path.h        # Path parsing for SFTP/HTTP
│   ├── ssh_session.h
│   ├── xios.h
│   └── z80_runner.h
├── src/
│   ├── banked_mem.cpp
│   ├── console.cpp
│   ├── disk.cpp
│   ├── http_server.cpp    # HTTP file browser
│   ├── main.cpp
│   ├── mpm_cpu.cpp
│   ├── sftp_bridge.cpp    # SFTP/HTTP to Z80 bridge
│   ├── sftp_path.cpp
│   ├── ssh_session_libssh.cpp
│   ├── xios.cpp
│   └── z80_runner.cpp
├── tools/
│   └── mkboot.cpp        # Creates boot image from LDRBIOS + MPMLDR
├── scripts/
│   ├── build_all.sh      # Master build (runs all steps)
│   ├── build_asm.sh      # Assemble Z80, build C++
│   ├── build_hd1k.sh     # Create hd1k disk images
│   └── gensys.sh         # Run GENSYS to create MPM.SYS
├── archive/              # Unused/experimental tools (kept for reference)
│   ├── tools/            # mkspr, mkdisk, mkmpm, patchmpm, etc.
│   └── scripts/          # fix_extents.sh, etc.
├── disks/                # Generated disk/boot images (in .gitignore)
├── build/                # CMake build directory
├── mpm2_external/        # MP/M II distribution (not in git)
└── keys/
    └── ssh_host_rsa_key (generated)
```

## External Dependencies

- **qkz80**: Z80 CPU emulator (symlinked from ../cpmemu/src/)
- **cpm_disk.py**: Disk image utility (from ../cpmemu/util/)
- **um80/ul80**: MACRO-80 compatible assembler/linker (from ../um80_and_friends/)
- **libssh**: SSH server library (optional, `brew install libssh`)
- **mpm2_external/**: MP/M II distribution and documentation

## License

GPL-3.0-or-later

## References

- MP/M II System Guide (mpm2_external/docs/)
- MP/M II Programmer's Guide
- CP/M BIOS specification
