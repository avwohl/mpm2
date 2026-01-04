#!/bin/bash
set -o errexit
#set -o verbose
#set -o xtrace
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
CPMEMU="${CPMEMU:-$HOME/src/cpmemu/src/cpmemu}"
WORK_DIR="/tmp/gensys_work"
CPM_DISK="${CPM_DISK:-$HOME/src/cpmemu/util/cpm_disk.py}"

# Number of consoles (default 4, matching asm/bnkxios.asm)
# must match asm/bnkxios.asm:
# NMBCNS:         EQU     4       ; consoles for SSH users
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

if [ ! -f "$CPM_DISK" ]; then
    echo "Error: cpm_disk.py not found at $CPM_DISK"
    echo "Set CPM_DISK environment variable to cpm_disk.py path"
    exit 1
fi

if [ ! -f "$DISKS_DIR/mpm2_hd1k.img" ]; then
    echo "Error: mpm2_hd1k.img not found. Run scripts/build_hd1k.sh first"
    exit 1
fi

# Create work directory
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# Copy required SPR files from distribution directory
# (disk image versions may be corrupted by cpm_disk.py extraction)
echo "Extracting SPR files from distribution..."
DIST_DIR="$PROJECT_DIR/mpm2_external/mpm2dist"
for f in RESBDOS.SPR BNKBDOS.SPR XDOS.SPR BNKXDOS.SPR TMP.SPR GENSYS.COM; do
    lf=$(echo "$f" | tr '[:upper:]' '[:lower:]')
    cp "$DIST_DIR/$f" "./$lf"
    echo "Extracted $lf -> ./$lf ($(wc -c < "./$lf" | tr -d ' ') bytes)"
done

# Copy our custom BNKXIOS.SPR (port-based I/O for emulator)
if [ -f "$ASM_DIR/bnkxios.spr" ]; then
    echo "Using custom BNKXIOS.SPR from $ASM_DIR"
    cp "$ASM_DIR/bnkxios.spr" bnkxios.spr
else
    echo "Error: Custom BNKXIOS.SPR not found at $ASM_DIR/bnkxios.spr"
    echo "Run scripts/build_asm.sh first"
    exit 1
fi

# Copy GENSYS config (forces binary mode for SPR/SYS files)
cp "$SCRIPT_DIR/gensys.cfg" gensys.cfg

# Run GENSYS under cpmemu using expect for readable prompt/response
echo "Running GENSYS ($NMBCNS consoles, 7 user banks, C0 common base)..."

# Check for expect
if ! command -v expect &> /dev/null; then
    echo "Error: 'expect' not found. Install with: brew install expect"
    exit 1
fi

# Create expect script with clear prompt/response pairs
cat > gensys_expect.exp << EXPECT_SCRIPT
set timeout 10
log_user 0
spawn $CPMEMU gensys.cfg

# System configuration prompts
expect "Use SYSTEM.DAT"           { send "N\r" }
expect "Top page"                 { send "\r" }
expect "Number of TMPs"           { send "$NMBCNS\r" }
expect "Number of Printers"       { send "\r" }
expect "Breakpoint RST"           { send "\r" }
expect "Compatibility Attributes" { send "\r" }
expect "system call user stacks"  { send "Y\r" }
expect "Z80 CPU"                  { send "\r" }
expect "ticks/second"             { send "\r" }
expect "System Drive"             { send "\r" }
expect "Temporary file drive"     { send "\r" }
expect "locked records/process"   { send "\r" }
expect "locked records/system"    { send "\r" }
expect "open files/process"       { send "\r" }
expect "open files/system"        { send "\r" }

# Memory configuration
# Note: MP/M II supports max 7 user memory segments
expect "Bank switched"            { send "\r" }
expect "memory segments"          { send "7\r" }
expect "Common memory base"       { send "C0\r" }
expect "Dayfile logging"          { send "\r" }

# Accept generated tables
expect "Accept new system data"   { send "\r" }
# Memory segment table: bank 0 (common) + 7 user banks = 8 entries
expect "Base,size,attrib,bank"    { send "\r" }
expect "Base,size,attrib,bank"    { send "\r" }
expect "Base,size,attrib,bank"    { send "\r" }
expect "Base,size,attrib,bank"    { send "\r" }
expect "Base,size,attrib,bank"    { send "\r" }
expect "Base,size,attrib,bank"    { send "\r" }
expect "Base,size,attrib,bank"    { send "\r" }
expect "Base,size,attrib,bank"    { send "\r" }
expect "Accept new memory segment" { send "\r" }

expect eof
EXPECT_SCRIPT

expect gensys_expect.exp > gensys.log 2>&1 || true

# Check if MPM.SYS was created
if [ ! -f "mpm.sys" ]; then
    echo "Error: GENSYS failed to create mpm.sys"
    cat gensys.log
    exit 1
fi

echo "MPM.SYS created ($(wc -c < mpm.sys | tr -d ' ') bytes)"

# Get serial number from RESBDOS.SPR and patch MPMLDR.COM
echo "Patching MPMLDR.COM serial number..."
# Use original MPMLDR.COM from distribution, not from disk image
# (disk image version may be corrupted or have wrong format)
cp "$PROJECT_DIR/mpm2_external/mpm2dist/MPMLDR.COM" mpmldr.com

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
cp mpm.sys "$DISKS_DIR/mpm.sys"
cp mpmldr.com "$DISKS_DIR/mpmldr.com"
cp boot.bin "$DISKS_DIR/mpm2boot.bin"

# Create system disk (rename intermediate image to final)
echo "Creating system disk..."
mv "$DISKS_DIR/mpm2_hd1k.img" "$DISKS_DIR/mpm2_system.img"
python3 "$CPM_DISK" delete "$DISKS_DIR/mpm2_system.img" mpm.sys mpmldr.com 2>/dev/null || true
python3 "$CPM_DISK" add "$DISKS_DIR/mpm2_system.img" mpm.sys mpmldr.com

echo ""
echo "Generation complete!"
echo ""
echo "Files created:"
echo "  Boot image:  $DISKS_DIR/mpm2boot.bin"
echo "  MPM.SYS:     $DISKS_DIR/mpm.sys"
echo "  System disk: $DISKS_DIR/mpm2_system.img"
echo ""
echo "To run:"
echo "  $BUILD_DIR/mpm2_emu -d A:$DISKS_DIR/mpm2_system.img"
echo ""
echo "Configuration:"
echo "  - $NMBCNS consoles"
echo "  - Common base at C000 (16KB common, 48KB user per bank)"
echo "  - 7 user memory banks (MP/M II maximum)"
