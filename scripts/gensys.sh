#!/bin/bash
# gensys.sh - Generate MPM.SYS using cpmemu and GENSYS.COM
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ASM_DIR="$PROJECT_DIR/asm"
DISKS_DIR="$PROJECT_DIR/disks"
DIST_DIR="$PROJECT_DIR/mpm2_external/mpm2dist"
BUILD_DIR="$PROJECT_DIR/build"
CPMEMU="${CPMEMU:-/home/wohl/src/cpmemu/src/cpmemu}"

# Check prerequisites
if [ ! -x "$CPMEMU" ]; then
    echo "Error: cpmemu not found at $CPMEMU"
    echo "Set CPMEMU environment variable to cpmemu path"
    exit 1
fi

if [ ! -f "$DIST_DIR/GENSYS.COM" ]; then
    echo "Error: GENSYS.COM not found in $DIST_DIR"
    exit 1
fi

# Build RESXIOS.SPR if needed
if [ ! -f "$DISKS_DIR/RESXIOS.SPR" ] || [ "$ASM_DIR/resxios.asm" -nt "$DISKS_DIR/RESXIOS.SPR" ]; then
    echo "Building RESXIOS.SPR..."
    cd "$ASM_DIR"
    z80asm resxios.asm -o resxios.bin
    "$BUILD_DIR/mkspr" resxios.bin "$DISKS_DIR/RESXIOS.SPR" 512
    rm -f resxios.bin
fi

# Create a working directory with all needed files
WORKDIR=$(mktemp -d)
trap "rm -rf $WORKDIR" EXIT

echo "Generating MPM.SYS..."
echo "Working directory: $WORKDIR"
echo ""

# Copy required files to work directory
cp "$DIST_DIR/GENSYS.COM" "$WORKDIR/"
cp "$DIST_DIR/RESBDOS.SPR" "$WORKDIR/"
cp "$DIST_DIR/XDOS.SPR" "$WORKDIR/"
cp "$DIST_DIR/BNKBDOS.SPR" "$WORKDIR/"
cp "$DIST_DIR/BNKXDOS.SPR" "$WORKDIR/"
cp "$DIST_DIR/TMP.SPR" "$WORKDIR/"

# Copy our RESXIOS.SPR (for non-banked mode)
cp "$DISKS_DIR/RESXIOS.SPR" "$WORKDIR/"

# Copy RSP files (optional)
for f in "$DIST_DIR"/*.RSP; do
    [ -f "$f" ] && cp "$f" "$WORKDIR/"
done

# Change to work directory
cd "$WORKDIR"

echo "Running GENSYS.COM..."
echo ""
echo "IMPORTANT: For this emulator, use these settings:"
echo "  Top page of operating system: FF"
echo "  Number of consoles: 1"
echo "  Number of printers: 1"
echo "  Breakpoint RST: 6"
echo "  Add system call user stacks: Y"
echo "  Z80 CPU: Y"
echo "  Number of ticks/second: 60"
echo "  Bank switched memory: N  <-- IMPORTANT!"
echo "  Number of memory segments: 1"
echo ""

"$CPMEMU" --z80 GENSYS.COM

# Check if MPM.SYS was created
if [ -f "$WORKDIR/MPM.SYS" ]; then
    mkdir -p "$DISKS_DIR"
    cp "$WORKDIR/MPM.SYS" "$DISKS_DIR/"
    echo ""
    echo "MPM.SYS created: $DISKS_DIR/MPM.SYS"
    ls -l "$DISKS_DIR/MPM.SYS"

    # Also copy SYSTEM.DAT if created
    if [ -f "$WORKDIR/SYSTEM.DAT" ]; then
        cp "$WORKDIR/SYSTEM.DAT" "$DISKS_DIR/"
        echo "SYSTEM.DAT created: $DISKS_DIR/SYSTEM.DAT"
    fi

    echo ""
    echo "Now rebuild the system disk with:"
    echo "  ./scripts/build_system.sh"
else
    echo ""
    echo "Error: MPM.SYS was not created"
    echo "Check GENSYS output for errors"
    exit 1
fi
