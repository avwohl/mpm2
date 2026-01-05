#!/bin/bash
set -o errexit
#set -o verbose
#set -o xtrace

# build_all.sh - Master build script for MP/M II Emulator
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs all build scripts in the correct order to produce a complete
# MP/M II system ready to boot.
#
# Usage:
#   build_all.sh [--tree=dri|src]
#
# Binary Trees:
#   dri (default) - Use original DRI binaries
#   src           - Build from source code first, then use source-built binaries

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Parse arguments
TREE="dri"

while [ $# -gt 0 ]; do
    case "$1" in
        --tree=*)
            TREE="${1#--tree=}"
            shift
            ;;
        --tree)
            TREE="$2"
            shift 2
            ;;
        -h|--help)
            cat <<EOF
Usage: $0 [--tree=dri|src]

Build a complete MP/M II system ready to boot.

Options:
    --tree=TREE     Binary tree to use: 'dri' (default) or 'src'
    -h, --help      Show this help message

Binary Trees:
    dri     Use original DRI binaries (fast, no compilation needed)
    src     Build from source using uplm80/um80/ul80, then use those binaries

Build Steps:
    1. [src only] Build source code (build_src.sh)
    2. Create disk image with binaries (build_hd1k.sh)
    3. Build assembly (LDRBIOS, BNKXIOS) and C++ tools (build_asm.sh)
    4. Generate MPM.SYS and boot image (gensys.sh)

EOF
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Validate tree selection
if [ "$TREE" != "dri" ] && [ "$TREE" != "src" ]; then
    echo "Error: Invalid tree '$TREE'. Use 'dri' or 'src'"
    exit 1
fi

echo "=============================================="
echo "MP/M II Complete Build"
echo "=============================================="
echo "Tree: $TREE"
echo ""

# Step 0: If using source tree, build from source first
if [ "$TREE" = "src" ]; then
    echo "Step 0: Building from source code..."
    "$SCRIPT_DIR/build_src.sh"
    echo ""
fi

# Step 1: Create hd1k disk image with MP/M II files
# (Must run before build_asm.sh so boot sector can be written to it)
echo "Step 1: Creating hd1k disk image..."
"$SCRIPT_DIR/build_hd1k.sh" --tree="$TREE"

# Step 2: Build assembly (LDRBIOS, BNKXIOS) and C++ tools
# (Writes boot sector to the disk image created in step 1)
echo ""
echo "Step 2: Building assembly and C++ tools..."
"$SCRIPT_DIR/build_asm.sh"

# Step 3: Run GENSYS to create MPM.SYS
echo ""
echo "Step 3: Running GENSYS to create MPM.SYS..."
"$SCRIPT_DIR/gensys.sh" --tree="$TREE"

echo ""
echo "=============================================="
echo "Build Complete! (tree=$TREE)"
echo "=============================================="
echo ""
echo "To run the emulator:"
echo "  $PROJECT_DIR/build/mpm2_emu -d A:$PROJECT_DIR/disks/mpm2_system.img"
echo ""
