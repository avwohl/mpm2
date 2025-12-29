# MP/M II Emulator

MP/M II (Multi-Programming Monitor for CP/M) emulator with SSH terminal access.

## Project Overview

This emulator runs MP/M II, Digital Research's multi-user, multi-tasking operating system for Z80-based computers. The primary use case is testing the uada80 Ada compiler which requires multi-user/multi-tasking capabilities.

## Architecture

### Threading Model

- **Z80 Thread**: Single thread running the Z80 CPU emulator with 60Hz timer interrupts for MP/M II scheduling
- **SSH Session Threads**: One thread per SSH connection, bridging the SSH stream to console I/O queues
- **Main Thread**: SSH accept loop and signal handling

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
| Z80 | `z80_thread.h/cpp` | CPU emulation thread with timer interrupts |
| Disk | `disk.h/cpp` | CP/M-compatible disk image I/O |
| SSH | `ssh_session.h/cpp` | wolfSSH-based terminal access |

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

### Loading Binary Data

**Always load binary data from assembled files rather than manually constructing byte sequences in code.** This applies to:
- Disk parameter blocks (DPB) - use DISKDEF.LIB macros in assembly
- BIOS/XIOS jump tables - assemble from source
- Boot images - use mkboot tool with assembled binaries

Manual byte poking leads to subtle errors. Use the tested DISKDEF.LIB macros from the MP/M II distribution for disk parameters.

### Disk Formats

**IMPORTANT: Use scripts/*.sh scripts to build disks **

**IMPORTANT: Use the project's custom `diskdefs` file, NOT RomWBW or standard cpmtools diskdefs.**

The emulator uses disk images with **NO SECTOR SKEW** for simplicity. Standard formats like ibm-3740 have skew factor 6, which complicates sector translation. Our custom diskdefs create images with skew=0.

```bash
# Always use the project diskdefs
export DISKDEFS=$PWD/diskdefs

# Create 8" SSSD image (for floppy boot)
dd if=/dev/zero bs=128 count=2002 of=disks/mpm2_sssd.img
mkfs.cpm -f mpm2-sssd disks/mpm2_sssd.img
cpmcp -f mpm2-sssd disks/mpm2_sssd.img mpm2_external/mpm2dist/*.* 0:

# Create hd1k image (8MB)
dd if=/dev/zero bs=1024 count=8192 of=disks/mpm2_hd1k.img
mkfs.cpm -f mpm2-hd1k disks/mpm2_hd1k.img
cpmcp -f mpm2-hd1k disks/mpm2_hd1k.img mpm2_external/mpm2dist/*.* 0:
```

### Custom Diskdefs (diskdefs file)

The project includes a `diskdefs` file with NO SKEW versions of standard formats:

| Format | Description | Geometry |
|--------|-------------|----------|
| mpm2-sssd | 8" SSSD (ibm-3740 compatible) | 77 trk, 26 sec, 128 bytes, 1K blocks |
| mpm2-hd1k | 8MB hard disk | 1024 trk, 16 sec, 512 bytes, 4K blocks |

Both formats have:
- `skew 0` - no sector interleave
- `boottrk 2` - 2 reserved system tracks
- Compatible DPB with LDRBIOS

**DO NOT** use standard ibm-3740 or wbw_hd1k diskdefs - they have sector skew that the emulator doesn't handle.

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
- wolfSSL and wolfSSH (optional, for SSH support)

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

For SSH support, install wolfSSL and wolfSSH first:
```bash
# wolfSSL - must disable FORTIFY_SOURCE to avoid false positive buffer overflow detection in RSA
git clone https://github.com/wolfSSL/wolfssl.git
cd wolfssl && ./autogen.sh
CFLAGS="-D_FORTIFY_SOURCE=0 -O2" ./configure --enable-ssh --prefix=$HOME/local
make && make install

# wolfSSH
git clone https://github.com/wolfSSL/wolfssh.git
cd wolfssh && ./autogen.sh
./configure --with-wolfssl=$HOME/local --prefix=$HOME/local
make && make install

# Build mpm2 with wolfSSH
cd mpm2/build
cmake -DCMAKE_PREFIX_PATH=$HOME/local ..
make
```

### Generate SSH Host Key

wolfSSH requires DER format keys:

```bash
mkdir -p keys
ssh-keygen -t rsa -b 2048 -m PEM -f keys/ssh_host_rsa_key -N ''
openssl rsa -in keys/ssh_host_rsa_key -outform DER -out keys/ssh_host_rsa_key.der
```

## Usage

```bash
./mpm2_emu [options]

Options:
  -p, --port PORT       SSH listen port (default: 2222)
  -k, --key FILE        Host key file (default: keys/ssh_host_rsa_key)
  -d, --disk A:FILE     Mount disk image on drive A-P
  -b, --boot FILE       Boot image file (MPMLDR + MPM.SYS)
  -s, --sys FILE        Load MPM.SYS directly (bypass MPMLDR)
  -l, --local           Enable local console output
  -t, --timeout SECS    Boot timeout for debugging
  -x, --xios ADDR       XIOS base address in hex (default: FC00)

Examples:
  # Standard boot (uses MPMLDR)
  ./mpm2_emu -d A:system.dsk -d B:work.dsk -b mpm2boot.bin

  # Direct MPM.SYS load (bypasses MPMLDR)
  ./mpm2_emu -l -s disks/MPM.SYS -d A:system.dsk
```

Connect via SSH:
```bash
ssh -p 2222 user@localhost
```

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

### Working with Disk Images

Use the project's `diskdefs` file (no skew) for all disk operations:

```bash
export DISKDEFS=$PWD/diskdefs

# Create 8" SSSD boot disk with MP/M II files
dd if=/dev/zero bs=128 count=2002 of=disks/mpm2_sssd.img
mkfs.cpm -f mpm2-sssd disks/mpm2_sssd.img
cpmcp -f mpm2-sssd disks/mpm2_sssd.img mpm2_external/mpm2dist/*.* 0:

# List files (use -T raw on macOS)
cpmls -f mpm2-sssd disks/mpm2_sssd.img

# Copy a single file
cpmcp -f mpm2-sssd disks/mpm2_sssd.img myfile.com 0:
```

For hd1k (8MB) images:
```bash
dd if=/dev/zero bs=1024 count=8192 of=disks/mpm2_hd1k.img
mkfs.cpm -f mpm2-hd1k disks/mpm2_hd1k.img
cpmcp -f mpm2-hd1k disks/mpm2_hd1k.img mpm2_external/mpm2dist/*.* 0:
```

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
├── diskdefs              # Custom cpmtools diskdefs (NO SKEW)
├── asm/
│   ├── ldrbios.asm       # Loader BIOS for 8" SSSD (floppy)
│   ├── bnkxios.asm  # Banked XIOS (I/O port dispatch)
│   └── *.bin, *.SPR      # Generated (in .gitignore)
├── include/
│   ├── banked_mem.h
│   ├── console.h
│   ├── console_queue.h
│   ├── disk.h
│   ├── ssh_session.h
│   ├── xios.h
│   └── z80_thread.h
├── src/
│   ├── banked_mem.cpp
│   ├── console.cpp
│   ├── disk.cpp
│   ├── main.cpp
│   ├── ssh_session.cpp
│   ├── xios.cpp
│   ├── z80_thread.cpp
│   └── qkz80*.{h,cc} -> symlinks to cpmemu
├── scripts/
│   ├── build_all.sh    # Master build (runs all steps)
│   ├── build_asm.sh    # Assemble Z80, build C++
│   ├── build_hd1k.sh   # Create hd1k disk images
│   └── gensys.sh       # Run GENSYS to create MPM.SYS
├── disks/              # Generated disk/boot images (in .gitignore)
├── build/              # CMake build directory
├── mpm2_external/      # MP/M II distribution (not in git)
└── keys/
    └── ssh_host_rsa_key (generated)
```

## External Dependencies

- **qkz80**: Z80 CPU emulator (symlinked from ../cpmemu/src/)
- **um80/ul80**: MACRO-80 compatible assembler/linker (from ../um80_and_friends/)
- **cpmtools**: CP/M disk image utilities (with RomWBW diskdefs for hd1k format)
- **wolfSSH**: SSH server library (optional)
- **mpm2_external/**: MP/M II distribution and documentation

## License

GPL-3.0-or-later

## References

- MP/M II System Guide (mpm2_external/docs/)
- MP/M II Programmer's Guide
- CP/M BIOS specification
