#!/bin/bash
# build_all.sh - Master build script for MP/M II Emulator
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs all build scripts in the correct order to produce a complete
# MP/M II system ready to boot.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=============================================="
echo "MP/M II Complete Build"
echo "=============================================="
echo ""

# Step 1: Build assembly (LDRBIOS, BNKXIOS) and C++ tools
echo "Step 1: Building assembly and C++ tools..."
"$SCRIPT_DIR/build_asm.sh" "$@"

# Step 2: Create hd1k disk image with MP/M II distribution files
echo ""
echo "Step 2: Creating hd1k disk image..."
"$SCRIPT_DIR/build_hd1k.sh"

# Step 3: Run GENSYS to create MPM.SYS
echo ""
echo "Step 3: Running GENSYS to create MPM.SYS..."
"$SCRIPT_DIR/gensys.sh"

echo ""
echo "=============================================="
echo "Build Complete!"
echo "=============================================="
echo ""
echo "To run the emulator:"
echo "  $PROJECT_DIR/build/mpm2_emu -b $PROJECT_DIR/disks/boot_hd1k_4con.bin -d A:$PROJECT_DIR/disks/mpm_system_4con.img"
echo ""
