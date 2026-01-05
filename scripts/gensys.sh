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
# 3. Patches MPMLDR.COM serial number to match (DRI tree only)
# 4. Creates boot image and disk
#
# Usage:
#   gensys.sh [--tree=dri|src] [num_consoles]
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
TOOLS_DIR="$PROJECT_DIR/tools"
CPMEMU="${CPMEMU:-$HOME/src/cpmemu/src/cpmemu}"
WORK_DIR="/tmp/gensys_work"
CPM_DISK="${CPM_DISK:-$HOME/src/cpmemu/util/cpm_disk.py}"

# Parse arguments
TREE="dri"  # Default to DRI binaries
NMBCNS=4    # Default console count

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
        [0-9]*)
            NMBCNS="$1"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--tree=dri|src] [num_consoles]"
            exit 1
            ;;
    esac
done

# Validate tree selection
if [ "$TREE" != "dri" ] && [ "$TREE" != "src" ]; then
    echo "Error: Invalid tree '$TREE'. Use 'dri' or 'src'"
    exit 1
fi

# Set binary directory based on tree
BIN_DIR="$PROJECT_DIR/bin/$TREE"

echo "MP/M II System Generation"
echo "========================="
echo "Tree:     $TREE"
echo "Consoles: $NMBCNS"
echo "Binaries: $BIN_DIR"
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

# Create clean work directory (remove old files that could interfere with GENSYS prompts)
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# Copy required SPR files from selected tree
# GENSYS.COM always from DRI (it's a build tool, no source available)
echo "Copying SPR files from $TREE tree..."
for f in RESBDOS.SPR BNKBDOS.SPR XDOS.SPR BNKXDOS.SPR TMP.SPR; do
    lf=$(echo "$f" | tr '[:upper:]' '[:lower:]')
    if [ -f "$BIN_DIR/$f" ]; then
        cp "$BIN_DIR/$f" "./$lf"
        echo "  $lf ($(wc -c < "./$lf" | tr -d ' ') bytes) [$TREE]"
    else
        echo "Error: $f not found in $BIN_DIR"
        exit 1
    fi
done
# GENSYS.COM from DRI (no source available)
DRI_BIN_DIR="$PROJECT_DIR/bin/dri"
cp "$DRI_BIN_DIR/GENSYS.COM" "./gensys.com"
echo "  gensys.com ($(wc -c < gensys.com | tr -d ' ') bytes) [DRI - tool]"

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

# Remove any leftover SYSTEM.DAT that would cause GENSYS to skip first prompt
rm -f system.dat SYSTEM.DAT

# Run GENSYS under cpmemu using expect for readable prompt/response
echo "Running GENSYS ($NMBCNS consoles, 7 user banks, C0 common base)..."

# Check for expect
if ! command -v expect &> /dev/null; then
    echo "Error: 'expect' not found. Install with: brew install expect"
    exit 1
fi

# Create expect script with clear prompt/response pairs
# Uses short timeout (2s) and fails immediately on any timeout
cat > gensys_expect.exp << EXPECT_SCRIPT
set timeout 2
log_user 0

# Helper proc that fails on timeout
proc prompt {pattern response} {
    expect {
        \$pattern { send "\$response\r" }
        timeout { puts stderr "ERROR: Timeout waiting for: \$pattern"; exit 1 }
        eof { puts stderr "ERROR: Unexpected EOF waiting for: \$pattern"; exit 1 }
    }
}

spawn $CPMEMU gensys.cfg

# System configuration prompts
# Note: "Use SYSTEM.DAT" only appears if file exists - we delete it above
prompt "Top page"                 ""
prompt "Number of TMPs"           "$NMBCNS"
prompt "Number of Printers"       ""
prompt "Breakpoint RST"           ""
prompt "Compatibility Attributes" ""
prompt "system call user stacks"  "Y"
prompt "Z80 CPU"                  ""
prompt "ticks/second"             ""
prompt "System Drive"             ""
prompt "Temporary file drive"     ""
prompt "locked records/process"   ""
prompt "locked records/system"    ""
prompt "open files/process"       ""
prompt "open files/system"        ""

# Memory configuration
prompt "Bank switched"            ""
prompt "memory segments"          "7"
prompt "Common memory base"       "C0"
prompt "Dayfile logging"          ""

# Accept generated tables
prompt "Accept new system data"   ""
# Memory segment table: bank 0 (common) + 7 user banks = 8 entries
prompt "Base,size,attrib,bank"    ""
prompt "Base,size,attrib,bank"    ""
prompt "Base,size,attrib,bank"    ""
prompt "Base,size,attrib,bank"    ""
prompt "Base,size,attrib,bank"    ""
prompt "Base,size,attrib,bank"    ""
prompt "Base,size,attrib,bank"    ""
prompt "Base,size,attrib,bank"    ""
prompt "Accept new memory segment" ""

# Wait for explicit success message
expect {
    "GENSYS DONE" { exit 0 }
    timeout { puts stderr "ERROR: GENSYS timed out waiting for completion"; exit 1 }
    eof { puts stderr "ERROR: GENSYS exited unexpectedly"; exit 1 }
}
EXPECT_SCRIPT

if ! expect gensys_expect.exp 2>&1; then
    echo "Error: GENSYS failed - see output above"
    exit 1
fi

# Check if MPM.SYS was created
if [ ! -f "mpm.sys" ]; then
    echo "Error: GENSYS failed to create mpm.sys"
    cat gensys.log
    exit 1
fi

echo "MPM.SYS created ($(wc -c < mpm.sys | tr -d ' ') bytes)"

# Get MPMLDR.COM based on tree selection
echo "Setting up MPMLDR.COM..."
if [ "$TREE" = "src" ]; then
    # Source-built MPMLDR has serial check disabled - no patching needed
    if [ ! -f "$BIN_DIR/MPMLDR.COM" ]; then
        echo "Error: Source-built MPMLDR.COM not found at $BIN_DIR/MPMLDR.COM"
        echo "Run scripts/build_src.sh first"
        exit 1
    fi
    cp "$BIN_DIR/MPMLDR.COM" mpmldr.com
    echo "  mpmldr.com ($(wc -c < mpmldr.com | tr -d ' ') bytes) [SOURCE - no serial patch needed]"
else
    # DRI MPMLDR needs serial number patching
    cp "$BIN_DIR/MPMLDR.COM" mpmldr.com
    # Patch serial number: copy 2 bytes from RESBDOS.SPR offset 509-510 to MPMLDR offset 133-134
    # The serial is 6 bytes after "RESEARCH " at offset 0x1F9-0x1FE in RESBDOS.SPR
    # MPMLDR has matching bytes at offset 129-134, bytes 5-6 (133-134) must match
    python3 "$TOOLS_DIR/dri_patch.py" -o mpmldr.com --base mpmldr.com \
        --copy resbdos.spr:509:2@133
    echo "  mpmldr.com ($(wc -c < mpmldr.com | tr -d ' ') bytes) [DRI - serial patched]"
fi

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
