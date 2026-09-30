# Project Structure and Internals

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
