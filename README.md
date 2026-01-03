# MP/M II Emulator

A Z80-based MP/M II emulator with SSH terminal access. Multiple users can connect simultaneously to run CP/M-compatible software.

**[MP/M II Command Reference](docs/mpm2_summary.pdf)** - Complete guide to all commands and utilities

## Quick Start

```bash
# Build everything
./scripts/build_all.sh

# Run with local console
./build/mpm2_emu -l -d A:disks/mpm2_system.img

# Or run with SSH access (connect from another terminal)
./build/mpm2_emu -d A:disks/mpm2_system.img
ssh -p 2222 user@localhost
```

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

```bash
cd mpm2
./scripts/build_all.sh
```

This runs three steps:
1. **build_hd1k.sh** - Creates 8MB disk image with MP/M II files
2. **build_asm.sh** - Assembles LDRBIOS and BNKXIOS, builds C++ emulator, writes boot sector
3. **gensys.sh** - Runs GENSYS to create MPM.SYS (4 consoles, 7 memory banks)

Output: `disks/mpm2_system.img` - bootable disk with MP/M II

### Generate SSH Host Key

```bash
mkdir -p keys
ssh-keygen -t rsa -b 2048 -m PEM -f keys/ssh_host_rsa_key -N ''
```

## Running

```bash
./build/mpm2_emu [options] -d A:diskimage

Options:
  -d, --disk A:FILE     Mount disk image (required)
  -l, --local           Local console mode (output to stdout)
  -p, --port PORT       SSH listen port (default: 2222)
  -k, --key FILE        Host key file (default: keys/ssh_host_rsa_key)
  -t, --timeout SECS    Timeout for debugging
  -h, --help            Show help
```

The emulator boots from sector 0 of the disk mounted as drive A.

### Examples

```bash
# Local console - see output directly
./build/mpm2_emu -l -d A:disks/mpm2_system.img

# SSH mode - connect via ssh
./build/mpm2_emu -d A:disks/mpm2_system.img
ssh -p 2222 user@localhost

# Custom SSH port
./build/mpm2_emu -p 2223 -d A:disks/mpm2_system.img
```

## Project Structure

```
mpm2/
├── scripts/
│   ├── build_all.sh      # Master build script
│   ├── build_hd1k.sh     # Create disk image with MP/M II files
│   ├── build_asm.sh      # Assemble Z80 code, build C++, write boot sector
│   └── gensys.sh         # Generate MPM.SYS
├── asm/
│   ├── coldboot.asm      # Boot sector (loads MPMLDR + LDRBIOS)
│   ├── ldrbios.asm       # Loader BIOS for boot phase
│   └── bnkxios.asm       # Runtime XIOS (I/O port dispatch)
├── src/                  # C++ emulator source
├── include/              # C++ headers
├── build/                # CMake build directory (generated)
├── disks/                # Disk images (generated)
└── mpm2_external/        # MP/M II distribution (not in git)
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
