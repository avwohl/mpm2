#!/bin/bash
set -o errexit
#set -o verbose
#set -o xtrace
# gensys.sh - Generate MP/M II system using Python GENSYS
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This script generates an MP/M II system:
# 1. Copies SPR files to work directory
# 2. Runs Python gensys.py with JSON configuration
# 3. Patches MPMLDR.COM serial number to match (DRI tree only)
# 4. Creates boot image and disk
#
# Usage:
#   gensys.sh [--tree=dri|src] [num_consoles]
#
# Default configuration:
# - 4 consoles
# - Common base at C000 (48KB user memory per bank)
# - 7 user memory banks
# - Top of memory at FF00

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"
ASM_DIR="$PROJECT_DIR/asm"
DISKS_DIR="$PROJECT_DIR/disks"
TOOLS_DIR="$PROJECT_DIR/tools"
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

echo "MP/M II System Generation (Python GENSYS)"
echo "=========================================="
echo "Tree:     $TREE"
echo "Consoles: $NMBCNS"
echo "Binaries: $BIN_DIR"
echo ""

if [ ! -f "$CPM_DISK" ]; then
    echo "Error: cpm_disk.py not found at $CPM_DISK"
    echo "Set CPM_DISK environment variable to cpm_disk.py path"
    exit 1
fi

if [ ! -f "$DISKS_DIR/mpm2_hd1k.img" ]; then
    echo "Error: mpm2_hd1k.img not found. Run scripts/build_hd1k.sh first"
    exit 1
fi

# Create clean work directory
rm -rf "$WORK_DIR"
mkdir -p "$WORK_DIR"
cd "$WORK_DIR"

# Copy required SPR files from selected tree
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

# Copy our custom BNKXIOS.SPR (port-based I/O for emulator)
if [ -f "$ASM_DIR/bnkxios.spr" ]; then
    echo "Using custom BNKXIOS.SPR from $ASM_DIR"
    cp "$ASM_DIR/bnkxios.spr" bnkxios.spr
else
    echo "Error: Custom BNKXIOS.SPR not found at $ASM_DIR/bnkxios.spr"
    echo "Run scripts/build_asm.sh first"
    exit 1
fi

# Copy SFTP RSP modules if present
HAVE_SFTP=false
if [ -f "$ASM_DIR/SFTP.RSP" ] && [ -f "$ASM_DIR/SFTP.BRS" ]; then
    echo "Copying SFTP RSP modules from $ASM_DIR"
    cp "$ASM_DIR/SFTP.RSP" sftp.rsp
    cp "$ASM_DIR/SFTP.BRS" sftp.brs
    echo "  sftp.rsp ($(wc -c < sftp.rsp | tr -d ' ') bytes)"
    echo "  sftp.brs ($(wc -c < sftp.brs | tr -d ' ') bytes)"
    HAVE_SFTP=true
else
    echo "Note: SFTP RSP not found - run scripts/build_sftp_rsp.sh to build"
fi

# Generate JSON configuration for Python GENSYS
echo "Generating GENSYS configuration..."
cat > gensys_config.json << EOF
{
  "mem_top": 255,
  "common_base": 192,
  "num_consoles": $NMBCNS,
  "num_printers": 1,
  "bank_switched": true,
  "z80_cpu": true,
  "sys_call_stks": true,
  "banked_bdos": true,
  "day_file": true,
  "ticks_per_second": 60,
  "system_drive": 1,
  "temp_file_drive": 1,
  "max_locked_records": 16,
  "total_locked_records": 32,
  "max_open_files": 16,
  "total_open_files": 32,
  "num_mem_segments": 7,
  "breakpoint_rst": 6,
  "spr_dir": ".",
  "resbdos_spr": "resbdos.spr",
  "xdos_spr": "xdos.spr",
  "bnkxios_spr": "bnkxios.spr",
  "bnkbdos_spr": "bnkbdos.spr",
  "bnkxdos_spr": "bnkxdos.spr",
  "tmp_spr": "tmp.spr",
EOF

# Add RSPs if available
if [ "$HAVE_SFTP" = true ]; then
    cat >> gensys_config.json << EOF
  "rsps": [
    {
      "name": "SFTP",
      "rsp": "sftp.rsp",
      "brs": "sftp.brs"
    }
  ],
EOF
else
    cat >> gensys_config.json << EOF
  "rsps": [],
EOF
fi

cat >> gensys_config.json << EOF
  "output": "mpm.sys",
  "system_dat": "system.dat"
}
EOF

# Run Python GENSYS
echo ""
echo "Running Python GENSYS ($NMBCNS consoles, 7 user banks, C0 common base)..."
python3 "$TOOLS_DIR/gensys.py" gensys_config.json

# Check if MPM.SYS was created
if [ ! -f "mpm.sys" ]; then
    echo "Error: GENSYS failed to create mpm.sys"
    exit 1
fi

echo ""
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

# Create boot image (16KB for system tracks)
echo "Creating boot image..."
"$BUILD_DIR/mkboot" \
    -l "$ASM_DIR/ldrbios.bin" \
    -b bnkxios.spr \
    -m mpmldr.com \
    -s 16384 \
    -o boot.bin

# Copy files to project
echo "Saving files to project..."
cp mpm.sys "$DISKS_DIR/mpm.sys"
cp mpmldr.com "$DISKS_DIR/mpmldr.com"

# Create system disk (rename intermediate image to final)
echo "Creating system disk..."
mv "$DISKS_DIR/mpm2_hd1k.img" "$DISKS_DIR/mpm2_system.img"

# Write boot image to system tracks
echo "Writing boot image to system tracks..."
python3 "$CPM_DISK" write-boot "$DISKS_DIR/mpm2_system.img" boot.bin

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
echo "  $BUILD_DIR/mpm2_emu -b $DISKS_DIR/mpm2boot.bin -d A:$DISKS_DIR/mpm2_system.img"
echo ""
echo "Configuration:"
echo "  - $NMBCNS consoles"
echo "  - Common base at C000 (16KB common, 48KB user per bank)"
echo "  - 7 user memory banks (MP/M II maximum)"
