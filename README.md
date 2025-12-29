# MP/M II Emulator

A Z80-based MP/M II emulator with SSH terminal access. Multiple users can connect simultaneously to run CP/M-compatible software.

## Quick Start

```bash
# After setting up dependencies (see below)
./scripts/build_all.sh
./build/mpm2_emu -b disks/boot_hd1k_4con.bin -d A:disks/mpm_system_4con.img

# Connect from another terminal
ssh -p 2222 user@localhost
```

## Prerequisites

### Required Dependencies

| Dependency | Purpose | Installation |
|------------|---------|--------------|
| CMake 3.16+ | Build system | `brew install cmake` or `apt install cmake` |
| C++17 compiler | Compile emulator | Xcode (macOS) or `apt install g++` |
| Python 3 | Build scripts, um80/ul80 | Usually pre-installed |
| cpmtools | CP/M disk image tools | `brew install cpmtools` or `apt install cpmtools` |

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

### RomWBW Disk Definitions

The build scripts need RomWBW's diskdefs file for the hd1k disk format:

```bash
# Download RomWBW (only need the diskdefs file)
cd ~/esrc  # or any location
wget https://github.com/wwarthen/RomWBW/releases/download/v3.5.1/RomWBW-v3.5.1.zip
unzip RomWBW-v3.5.1.zip

# Set environment variable (add to ~/.bashrc or ~/.zshrc)
export DISKDEFS="$HOME/esrc/RomWBW-v3.5.1/Tools/cpmtools/diskdefs"
```

### Optional: SSH Support

For network access via SSH (recommended for multi-user):

```bash
# wolfSSL
git clone https://github.com/wolfSSL/wolfssl.git
cd wolfssl && ./autogen.sh
CFLAGS="-D_FORTIFY_SOURCE=0 -O2" ./configure --enable-ssh --prefix=$HOME/local
make && make install
cd ..

# wolfSSH
git clone https://github.com/wolfSSL/wolfssh.git
cd wolfssh && ./autogen.sh
./configure --with-wolfssl=$HOME/local --prefix=$HOME/local
make && make install
cd ..
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
# Just build C++ emulator
mkdir build && cd build
cmake ..
make

# With SSH support
cmake -DCMAKE_PREFIX_PATH=$HOME/local ..
make
```

### Generate SSH Host Key

```bash
mkdir -p keys
ssh-keygen -t rsa -b 2048 -m PEM -f keys/ssh_host_rsa_key -N ''
openssl rsa -in keys/ssh_host_rsa_key -outform DER -out keys/ssh_host_rsa_key.der
```

## Running

```bash
./build/mpm2_emu [options]

Options:
  -p, --port PORT       SSH listen port (default: 2222)
  -k, --key FILE        Host key file (default: keys/ssh_host_rsa_key)
  -d, --disk A:FILE     Mount disk image on drive A-P
  -b, --boot FILE       Boot image file (MPMLDR + MPM.SYS)
  -s, --sys FILE        Load MPM.SYS directly (bypass MPMLDR)
  -l, --local           Enable local console output
  -t, --timeout SECS    Boot timeout for debugging
  -h, --help            Show help

Examples:
  # Standard boot with SSH (uses MPMLDR)
  ./build/mpm2_emu -b disks/boot_hd1k_4con.bin -d A:disks/mpm_system_4con.img

  # Direct MPM.SYS load (bypasses MPMLDR)
  ./build/mpm2_emu -l -s disks/MPM.SYS -d A:disks/mpm_system_4con.img

  # With local console output
  ./build/mpm2_emu -l -b disks/boot_hd1k_4con.bin -d A:disks/mpm_system_4con.img

  # Debug with 5 second timeout
  ./build/mpm2_emu -t 5 -l -b disks/boot_hd1k_4con.bin -d A:disks/mpm_system_4con.img
```

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

### Build fails with "diskdefs not found"
Set `DISKDEFS` environment variable to RomWBW diskdefs path.

### SSH connection refused
Ensure the emulator is running and check if port 2222 is available.

## License

GPL-3.0-or-later

## References

- [MP/M II System Guide](mpm2_external/docs/)
- [RomWBW](https://github.com/wwarthen/RomWBW) - hd1k disk format
- [cpmtools](https://github.com/lipro-cpm4l/cpmtools) - CP/M disk utilities
