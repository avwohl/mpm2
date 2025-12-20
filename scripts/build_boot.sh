#!/bin/bash
# build_boot.sh - Build MP/M II boot image
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ASM_DIR="$PROJECT_DIR/asm"
BUILD_DIR="$PROJECT_DIR/build"
DISKS_DIR="$PROJECT_DIR/disks"
EXTERNAL_DIR="$PROJECT_DIR/mpm2_external"

echo "Building MP/M II boot image..."
echo "==============================="

# Build assembly files
echo ""
echo "Assembling LDRBIOS and XIOS..."
cd "$ASM_DIR"
make clean
make

# Build C++ code
echo ""
echo "Building C++ emulator and tools..."
cd "$BUILD_DIR"
cmake ..
make -j$(nproc)

# Create boot image
echo ""
echo "Creating boot image..."
"$BUILD_DIR/mkboot" \
    -l "$ASM_DIR/ldrbios.bin" \
    -x "$ASM_DIR/xios.bin" \
    -m "$EXTERNAL_DIR/mpm2dist/MPMLDR.COM" \
    -o "$DISKS_DIR/boot.img"

echo ""
echo "Boot image created: $DISKS_DIR/boot.img"
echo ""
echo "To run: $BUILD_DIR/mpm2_emu -b $DISKS_DIR/boot.img -d A:$DISKS_DIR/system.dsk"
