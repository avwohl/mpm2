#!/bin/bash
# build_system.sh - Build complete MP/M II system from external distribution
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ASM_DIR="$PROJECT_DIR/asm"
BUILD_DIR="$PROJECT_DIR/build"
DISKS_DIR="$PROJECT_DIR/disks"
EXTERNAL_DIR="$PROJECT_DIR/mpm2_external"
DIST_DIR="$EXTERNAL_DIR/mpm2dist"

echo "Building MP/M II System"
echo "======================="

# Check prerequisites
if [ ! -d "$EXTERNAL_DIR" ]; then
    echo "Error: mpm2_external directory not found"
    echo "Please ensure MP/M II distribution files are in place"
    exit 1
fi

# Create directories
mkdir -p "$BUILD_DIR"
mkdir -p "$DISKS_DIR"

# Build assembly files
echo ""
echo "Step 1: Assembling LDRBIOS and XIOS..."
cd "$ASM_DIR"
make clean 2>/dev/null || true
make

# Build C++ code
echo ""
echo "Step 2: Building C++ emulator and tools..."
cd "$BUILD_DIR"
cmake ..
make -j$(nproc)

# Create boot image
echo ""
echo "Step 3: Creating boot image..."
"$BUILD_DIR/mkboot" \
    -l "$ASM_DIR/ldrbios.bin" \
    -x "$ASM_DIR/xios.bin" \
    -m "$DIST_DIR/MPMLDR.COM" \
    -o "$DISKS_DIR/boot.img"

# Create system disk with MP/M utilities
echo ""
echo "Step 4: Creating system disk..."

# Collect all files to add to disk
FILES_TO_ADD=""

# Add RSP (Resident System Process) files
for f in "$DIST_DIR"/*.RSP; do
    [ -f "$f" ] && FILES_TO_ADD="$FILES_TO_ADD $f"
done

# Add SPR (System Page Relocatable) files
for f in "$DIST_DIR"/*.SPR; do
    [ -f "$f" ] && FILES_TO_ADD="$FILES_TO_ADD $f"
done

# Add PRL (Page Relocatable) files - these are the utilities
for f in "$DIST_DIR"/*.PRL; do
    [ -f "$f" ] && FILES_TO_ADD="$FILES_TO_ADD $f"
done

# Add BRS (Banked Resident System Process) files
for f in "$DIST_DIR"/*.BRS; do
    [ -f "$f" ] && FILES_TO_ADD="$FILES_TO_ADD $f"
done

# Add COM files (development tools)
for f in "$DIST_DIR"/*.COM; do
    [ -f "$f" ] && FILES_TO_ADD="$FILES_TO_ADD $f"
done

# Create the system disk
"$BUILD_DIR/mkdisk" -o "$DISKS_DIR/system.img" $FILES_TO_ADD

echo ""
echo "=========================================="
echo "Build complete!"
echo ""
echo "Boot image:   $DISKS_DIR/boot.img"
echo "System disk:  $DISKS_DIR/system.img"
echo ""
echo "To run:"
echo "  $BUILD_DIR/mpm2_emu -b $DISKS_DIR/boot.img -d A:$DISKS_DIR/system.img"
echo ""
