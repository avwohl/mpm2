#!/bin/bash
# gensys.sh - Generate MP/M II system using GENSYS
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This script automates the GENSYS process:
# 1. Creates a BNKXIOS.SPR with the specified number of consoles
# 2. Runs GENSYS under cpmemu with default settings
# 3. Patches MPMLDR.COM serial number to match
# 4. Creates boot image and disk
#
# Default configuration:
# - 4 consoles
# - Common base at C000 (48KB user memory per bank)
# - 4 user memory banks (one per console)
# - Top of memory at FF00

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
ASM_DIR="$PROJECT_DIR/asm"
DISKS_DIR="$PROJECT_DIR/disks"
CPMEMU="${CPMEMU:-/Users/wohl/src/cpmemu/src/cpmemu}"
WORK_DIR="/tmp/gensys_work"

# Number of consoles (default 4)
# must match asm/bnkxio.asm
;NMBCNS:         EQU     4       ; consoles for SSH users
NMBCNS=${1:-4}

echo "MP/M II System Generation"
echo "========================="
echo "Consoles: $NMBCNS"
echo ""

# Check prerequisites
if [ ! -x "$CPMEMU" ]; then
    echo "Error: cpmemu not found at $CPMEMU"
    echo "Set CPMEMU environment variable to cpmemu path"
    exit 1
fi

if [ ! -f "$DISKS_DIR/mpm2_hd1k.img" ]; then
    echo "Error: mpm2_hd1k.img not found. Run scripts/build_hd1k.sh first"
    exit 1
fi

# Create work directory
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# Copy diskdefs to work directory (cpmtools looks for local diskdefs first)
if [ -f "$PROJECT_DIR/diskdefs" ]; then
    cp "$PROJECT_DIR/diskdefs" diskdefs
fi

# Extract required SPR files from distribution disk
echo "Extracting SPR files from distribution..."
for file in resbdos.spr bnkbdos.spr xdos.spr bnkxdos.spr tmp.spr gensys.com; do
    cpmcp -T raw -f wbw_hd1k "$DISKS_DIR/mpm2_hd1k.img" "0:$file" .
done

# Run GENSYS under cpmemu
# GENSYS prompts (see MP/M II System Implementor's Guide):
#   1. "Use SYSTEM.DAT?" -> N (don't use saved config)
#   2. "Number of memory segments (1-8)?" -> 4 (for 4 banks)
#   3. For each segment: base address (usually 0000 for each)
#   4. "Common base?" -> C0 (for C000H, giving 48KB user + 16KB common)
#   5. "Number of consoles?" -> 4
#   6. Various RSP/BRS includes
#   ... etc
# Here we provide: N, 4 segments, 0000 for each base, C0 common, defaults for rest
echo "Running GENSYS (4 banks, C0 common base)..."
echo -e "N\n4\n0000\n0000\n0000\n0000\nC0\n4\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n" | "$CPMEMU" gensys.com > gensys.log 2>&1 || true

# Check if MPM.SYS was created
if [ ! -f "mpm.sys" ]; then
    echo "Error: GENSYS failed to create mpm.sys"
    cat gensys.log
    exit 1
fi

echo "MPM.SYS created ($(wc -c < mpm.sys | tr -d ' ') bytes)"

# Get serial number from RESBDOS.SPR and patch MPMLDR.COM
echo "Patching MPMLDR.COM serial number..."
cpmcp -T raw -f wbw_hd1k "$DISKS_DIR/mpm2_hd1k.img" "0:mpmldr.com" mpmldr.com

# Extract serial from RESBDOS.SPR
# The serial is 6 bytes after "RESEARCH " at offset 0x1F9-0x1FE: 00 14 01 00 05 68
# MPMLDR has matching bytes at offset 129-134, but bytes 5-6 (offset 133-134) need patching
# MPMLDR 133-134 must match RESBDOS 509-510 (bytes 5-6 of the 6-byte serial)
SERIAL_B5=$(dd if=resbdos.spr bs=1 skip=509 count=1 2>/dev/null | xxd -p)
SERIAL_B6=$(dd if=resbdos.spr bs=1 skip=510 count=1 2>/dev/null | xxd -p)
# Patch MPMLDR at offset 133-134 with RESBDOS bytes 509-510
printf "\\x$SERIAL_B5" | dd of=mpmldr.com bs=1 seek=133 conv=notrunc 2>/dev/null
printf "\\x$SERIAL_B6" | dd of=mpmldr.com bs=1 seek=134 conv=notrunc 2>/dev/null

# Create boot image
echo "Creating boot image..."
"$BUILD_DIR/mkboot" \
    -l "$ASM_DIR/ldrbios.bin" \
    -b bnkxios.spr \
    -m mpmldr.com \
    -o boot.bin

# Copy files to project
echo "Saving files to project..."
cp mpm.sys "$DISKS_DIR/mpm_${NMBCNS}con.sys"
cp mpmldr.com "$DISKS_DIR/mpmldr_${NMBCNS}con.com"
cp boot.bin "$DISKS_DIR/boot_hd1k_${NMBCNS}con.bin"
cp bnkxios.spr "$ASM_DIR/BNKXIOS_${NMBCNS}con.spr"

# Create system disk
echo "Creating system disk..."
cp "$DISKS_DIR/mpm2_hd1k.img" "$DISKS_DIR/mpm_system_${NMBCNS}con.img"
cpmrm -T raw -f wbw_hd1k "$DISKS_DIR/mpm_system_${NMBCNS}con.img" "0:mpm.sys" 2>/dev/null || true
cpmrm -T raw -f wbw_hd1k "$DISKS_DIR/mpm_system_${NMBCNS}con.img" "0:mpmldr.com" 2>/dev/null || true
cpmcp -T raw -f wbw_hd1k "$DISKS_DIR/mpm_system_${NMBCNS}con.img" mpm.sys 0:
cpmcp -T raw -f wbw_hd1k "$DISKS_DIR/mpm_system_${NMBCNS}con.img" mpmldr.com 0:mpmldr.com

echo ""
echo "Generation complete!"
echo ""
echo "Files created:"
echo "  Boot image:  $DISKS_DIR/boot_hd1k_${NMBCNS}con.bin"
echo "  MPM.SYS:     $DISKS_DIR/mpm_${NMBCNS}con.sys"
echo "  System disk: $DISKS_DIR/mpm_system_${NMBCNS}con.img"
echo ""
echo "To run:"
echo "  $BUILD_DIR/mpm2_emu -b $DISKS_DIR/boot_hd1k_${NMBCNS}con.bin -d A:$DISKS_DIR/mpm_system_${NMBCNS}con.img"
echo ""
echo "Configuration:"
echo "  - $NMBCNS consoles"
echo "  - Common base at C000 (16KB common, 48KB user per bank)"
echo "  - 4 user memory banks"
