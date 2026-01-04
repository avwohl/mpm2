#!/bin/bash
set -o errexit

# build_src.sh - Build MP/M II from source code
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds all MP/M II utilities and system files from source using:
# - uplm80: PL/M-80 cross-compiler
# - um80: 8080/Z80 cross-assembler
# - ul80: Cross-linker
#
# Output goes to bin/src/

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TOOLS_DIR="$PROJECT_DIR/tools"
OUTPUT_DIR="$PROJECT_DIR/bin/src"

echo "=============================================="
echo "MP/M II Source Build"
echo "=============================================="
echo ""

# Check for required tools
check_tool() {
    if ! command -v "$1" &> /dev/null; then
        echo "Error: $1 not found in PATH"
        echo "Please install $1 or add it to your PATH"
        exit 1
    fi
}

echo "Checking required tools..."
check_tool um80
check_tool ul80
check_tool uplm80
echo "  All tools found"
echo ""

# Run the build script
echo "Building from source..."
python3 "$TOOLS_DIR/build.py" "$@"

echo ""
echo "=============================================="
echo "Source Build Complete"
echo "=============================================="
echo ""
echo "Output directory: $OUTPUT_DIR"
echo ""
if [ -d "$OUTPUT_DIR" ]; then
    echo "Built files:"
    ls -la "$OUTPUT_DIR"/*.COM "$OUTPUT_DIR"/*.PRL "$OUTPUT_DIR"/*.SPR "$OUTPUT_DIR"/*.RSP 2>/dev/null | head -20
    TOTAL=$(ls "$OUTPUT_DIR" 2>/dev/null | wc -l | tr -d ' ')
    echo ""
    echo "Total: $TOTAL files"
fi
