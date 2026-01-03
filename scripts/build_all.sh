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

# Step 1: Create hd1k disk image with MP/M II distribution files
# (Must run before build_asm.sh so boot sector can be written to it)
echo "Step 1: Creating hd1k disk image..."
"$SCRIPT_DIR/build_hd1k.sh"

# Step 2: Build assembly (LDRBIOS, BNKXIOS) and C++ tools
# (Writes boot sector to the disk image created in step 1)
echo ""
echo "Step 2: Building assembly and C++ tools..."
"$SCRIPT_DIR/build_asm.sh" "$@"

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
echo "  $PROJECT_DIR/build/mpm2_emu -d A:$PROJECT_DIR/disks/mpm2_system.img"
echo ""
