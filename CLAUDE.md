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
- **Lower 32KB (0x0000-0x7FFF)**: Banked memory, switchable per process
- **Upper 32KB (0x8000-0xFFFF)**: Common memory, shared by all processes

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
  -x, --xios ADDR       XIOS base address in hex (default: FC00)

Examples:
  ./mpm2_emu -d A:system.dsk -d B:work.dsk -b mpm2boot.bin
  ./mpm2_emu -p 2222 -d A:mpm2.dsk -b boot.img
```

Connect via SSH:
```bash
ssh -p 2222 user@localhost
```

## Boot Process

1. Load boot image (MPMLDR + MPM.SYS) into memory
2. Initialize XIOS jump table at configured base address
3. Start Z80 execution at 0x0100 (or configured entry point)
4. MPMLDR loads MPM.SYS and transfers control
5. MP/M II initializes and presents login prompt on consoles

## Disk Image Formats

Disk format is auto-detected based on file size:

| Format | Size | Geometry | Dir Entries |
|--------|------|----------|-------------|
| 8" SSSD | 250KB | 77 trk, 26 sec, 128 bytes | 64 |
| hd1k | 8MB (8,388,608 bytes) | 1024 trk, 16 sec, 512 bytes | 1024 |
| hd512 | 8.32MB (8,519,680 bytes) | 1040 trk, 16 sec, 512 bytes | 512 |

The **hd1k** format (from RomWBW) is recommended for MP/M II due to its larger size and directory capacity.

### Working with hd1k Images

A script is provided to create hd1k disk images with all MP/M II files:

```bash
# Create disk with all MP/M II distribution files
./scripts/build_hd1k.sh

# Create with custom output path
./scripts/build_hd1k.sh -o disks/my_mpm2.img

# Create empty formatted disk
./scripts/build_hd1k.sh --empty -o disks/blank.img
```

Or use cpmtools directly with the RomWBW diskdefs:
```bash
export DISKDEFS="$HOME/esrc/RomWBW-v3.5.1/Tools/cpmtools/diskdefs"

# On macOS, add -T raw flag to cpmls/cpmcp commands
# Create blank 8MB image
dd if=/dev/zero bs=1024 count=8192 of=mydisk.img
mkfs.cpm -f wbw_hd1k mydisk.img

# Copy files (use -T raw on macOS)
cpmcp -T raw -f wbw_hd1k mydisk.img myfile.com 0:
cpmls -T raw -f wbw_hd1k mydisk.img
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
├── asm/
│   ├── ldrbios_hd1k.asm  # Loader BIOS for hd1k disks
│   ├── bnkxios_port.asm  # Banked XIOS (I/O port dispatch)
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
