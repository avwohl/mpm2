# MP/M II Emulator

A Z80-based MP/M II emulator with SSH terminal access. Multiple users can connect simultaneously to run CP/M-compatible software.

Features:

- MP/M II V2.0 or V2.1, from Digital Research's binaries or built from DRI's source code
- 4 consoles over SSH, 7 memory banks, and five resident system processes
- SFTP and HTTP access to the files on the MP/M II disk
- Access log of HTTP, SSH and SFTP connections

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

## Building

Building needs CMake 3.16+, a C++17 compiler, Python 3.10+ and cpmemu 4.10.0 (github.com/avwohl/cpmemu, `make` in `cpmemu/src`).
The build also needs um80/ul80 and uplm80 (`pip install um80 uplm80`), and libssh for SSH access.
[docs/building.md](docs/building.md) has the required tool releases, the build steps and the `build_all.sh` options.

## Documentation

- [Building](docs/building.md) - prerequisites, tool releases, build steps, source build, V2.0 and V2.1, GENSYS, resident system processes
- [Running](docs/running.md) - SSH setup, command-line options, examples, access logging, troubleshooting
- [SFTP and HTTP file access](docs/file_transfer.md) - SFTP paths and commands, the HTTP file browser, the SFTP RSP
- [Testing](docs/testing.md) - `run_tests.sh` tests and `verify_dri.py`
- [Project structure and internals](docs/internals.md) - source layout and how the emulator boots MP/M II
- [MP/M II V2.1 reconstruction](docs/mpm2_v21.md) - every change between V2.0 and V2.1, and the evidence
- [MP/M II command summary](docs/mpm2_summary.md) - commands and utilities
- [CHANGELOG](CHANGELOG.md) - release notes

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
