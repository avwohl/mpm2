# MP/M II Emulator

A Z80-based MP/M II emulator with SSH terminal access. Multiple users can connect simultaneously to run CP/M-compatible software.

## Quick Start

```bash
# After setting up dependencies (see below)
./scripts/build_all.sh

# Run with direct MPM.SYS loading (recommended)
./build/mpm2_emu -l -s disks/mpm.sys -d A:disks/mpm2_system.img

# Connect from another terminal (if SSH enabled)
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

### Full Build (Recommended)

```bash
cd mpm2
./scripts/build_all.sh
```

This runs three steps:
1. **build_asm.sh** - Assembles LDRBIOS and BNKXIOS, builds C++ emulator
2. **build_hd1k.sh** - Creates 8MB disk image with MP/M II files
3. **gensys.sh** - Runs GENSYS to create MPM.SYS (4 consoles, 4 memory banks)

### Manual Build Steps

```bash
# Build C++ emulator (auto-detects libssh if installed)
mkdir build && cd build
cmake ..
make
```

### Generate SSH Host Key

```bash
mkdir -p keys
ssh-keygen -t rsa -b 2048 -m PEM -f keys/ssh_host_rsa_key -N ''
```

## Running

```bash
./build/mpm2_emu [options]

Options:
  -p, --port PORT       SSH listen port (default: 2222)
  -k, --key FILE        Host key file (default: keys/ssh_host_rsa_key)
  -d, --disk A:FILE     Mount disk image on drive A-P
  -b, --boot FILE       Boot image file (NOT WORKING - use -s instead)
  -s, --sys FILE        Load MPM.SYS directly (recommended)
  -l, --local           Enable local console output
  -t, --timeout SECS    Boot timeout for debugging
  -h, --help            Show help

Examples:
  # Direct MPM.SYS load (recommended - the only working boot method)
  ./build/mpm2_emu -l -s disks/mpm.sys -d A:disks/mpm2_system.img

  # With SSH support (no -l flag)
  ./build/mpm2_emu -s disks/mpm.sys -d A:disks/mpm2_system.img
```

**Note:** Boot via MPMLDR (`-b` option) is not currently working. Use direct MPM.SYS loading (`-s` option) instead.

Connect via SSH:
```bash
ssh -p 2222 user@localhost
```

## Project Structure

```
mpm2/
├── scripts/
│   ├── build_all.sh      # Master build script
│   ├── build_asm.sh      # Assemble Z80 code, build C++
│   ├── build_hd1k.sh     # Create disk images
│   └── gensys.sh         # Generate MPM.SYS
├── asm/
│   ├── ldrbios_hd1k.asm  # Loader BIOS for hd1k disks
│   └── bnkxios_port.asm  # Banked XIOS (I/O port dispatch)
├── src/                  # C++ emulator source
├── include/              # C++ headers
├── build/                # CMake build directory (generated)
├── disks/                # Disk images (generated)
└── mpm2_external/        # MP/M II distribution (not in git)
```

## How It Works

MP/M II is Digital Research's multi-user, multi-tasking operating system for Z80. This emulator:

1. Runs a Z80 CPU with 60Hz timer interrupts
2. Provides banked memory (4 banks, 48KB user + 16KB common each)
3. Emulates disk I/O via hd1k format images
4. Exposes 4 consoles via SSH connections

The XIOS (Extended I/O System) uses I/O port traps - Z80 code does `OUT (0xE0), A` and the emulator intercepts to handle the function.

## Troubleshooting

### "MPM SYS ?" error on boot
The disk needs a valid MPM.SYS matching the LDRBIOS disk format. Run `./scripts/gensys.sh` to regenerate.

### Build fails with "um80 not found"
Install um80/ul80: `pip install -e path/to/um80_and_friends`

### SSH connection refused
Ensure the emulator is running and check if port 2222 is available.

## License

GPL-3.0-or-later

## References

- [MP/M II System Guide](mpm2_external/docs/)
- [RomWBW](https://github.com/wwarthen/RomWBW) - hd1k disk format
- [cpmemu](https://github.com/avwohl/cpmemu) - Z80 emulator with cpm_disk.py utility
