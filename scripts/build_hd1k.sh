#!/bin/bash
# build_hd1k.sh - Create an hd1k disk image with MP/M II files
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The hd1k format (from RomWBW) provides:
# - 8 MB capacity (8,388,608 bytes)
# - 1024 directory entries (vs 64 on old floppy formats)
# - 512 bytes/sector, 16 sectors/track, 1024 tracks
# - No sector skew needed (skew 0)

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MPM2_DISKS="$PROJECT_DIR/mpm2_external/mpm2disks"
OUTPUT_DIR="$PROJECT_DIR/disks"
TEMP_DIR=""

# On macOS, cpmtools needs -T raw for disk images without libdsk drivers
# Detect platform and set appropriate flags
if [[ "$(uname)" == "Darwin" ]]; then
    CPMTOOLS_FLAGS="-T raw"
else
    CPMTOOLS_FLAGS=""
fi

# RomWBW diskdefs path - adjust this for your system
if [ -z "$DISKDEFS" ]; then
    for path in \
        "$HOME/esrc/RomWBW-v3.5.1/Tools/cpmtools/diskdefs" \
        "$HOME/src/RomWBW/Tools/cpmtools/diskdefs" \
        "/usr/local/share/cpmtools/diskdefs" \
        "/usr/share/cpmtools/diskdefs"
    do
        if [ -f "$path" ]; then
            export DISKDEFS="$path"
            break
        fi
    done
fi

# Check for required tools
check_tools() {
    local missing=0
    for tool in dd mkfs.cpm cpmcp cpmls; do
        if ! command -v "$tool" &>/dev/null; then
            echo "Error: Required tool '$tool' not found"
            missing=1
        fi
    done

    if [ -z "$DISKDEFS" ] || [ ! -f "$DISKDEFS" ]; then
        echo "Error: Cannot find RomWBW diskdefs file"
        echo "Set DISKDEFS environment variable to the path of diskdefs"
        echo "  export DISKDEFS=\$HOME/esrc/RomWBW-v3.5.1/Tools/cpmtools/diskdefs"
        missing=1
    fi

    if [ $missing -eq 1 ]; then
        exit 1
    fi
}

# Create a temporary directory for file extraction
setup_temp() {
    TEMP_DIR=$(mktemp -d)
    trap "rm -rf '$TEMP_DIR'" EXIT
}

# Extract files from an ibm-3740 disk image
extract_files() {
    local image="$1"
    local dest="$2"

    if [ ! -f "$image" ]; then
        echo "Warning: Disk image not found: $image"
        return 1
    fi

    echo "Extracting files from $(basename "$image")..."

    # Get list of files
    local files=$(cpmls $CPMTOOLS_FLAGS -f ibm-3740 "$image" 2>/dev/null | grep -v '^[0-9]:$' || true)

    for file in $files; do
        # cpmcp needs the user area prefix
        cpmcp $CPMTOOLS_FLAGS -f ibm-3740 "$image" "0:$file" "$dest/" 2>/dev/null || {
            echo "  Warning: Could not extract $file"
        }
    done

    return 0
}

# Create an 8MB hd1k disk image
create_hd1k() {
    local output="$1"

    echo "Creating 8MB hd1k disk image: $output"

    # Create blank 8MB image
    dd if=/dev/zero bs=1024 count=8192 of="$output" 2>/dev/null

    # Format as hd1k (mkfs.cpm doesn't need -T flag)
    mkfs.cpm -f wbw_hd1k "$output"

    echo "  Created $(stat -f%z "$output" 2>/dev/null || stat -c%s "$output") bytes"
}

# Copy files to hd1k image
copy_to_hd1k() {
    local image="$1"
    local src_dir="$2"

    echo "Copying files to hd1k image..."

    # Copy all files to user area 0
    local count=0
    for file in "$src_dir"/*; do
        if [ -f "$file" ]; then
            local name=$(basename "$file")
            cpmcp $CPMTOOLS_FLAGS -f wbw_hd1k "$image" "$file" "0:" 2>/dev/null && {
                count=$((count + 1))
            } || {
                echo "  Warning: Could not copy $name"
            }
        fi
    done

    echo "  Copied $count files"
}

# Show disk contents
show_contents() {
    local image="$1"

    echo ""
    echo "Disk contents:"
    cpmls $CPMTOOLS_FLAGS -l -f wbw_hd1k "$image" | head -40
    local total=$(cpmls $CPMTOOLS_FLAGS -f wbw_hd1k "$image" | wc -l)
    if [ "$total" -gt 40 ]; then
        echo "  ... and $((total - 40)) more files"
    fi
    echo ""
    echo "Total files: $total"
}

# Main
usage() {
    cat <<EOF
Usage: $0 [options] [output.img]

Create an hd1k (8MB) disk image with MP/M II files.

Options:
    -h, --help      Show this help message
    -d, --disks DIR Path to MP/M II disk images (default: mpm2_external/mpm2disks)
    -o, --output    Output image path (default: disks/mpm2_hd1k.img)
    --no-disk1      Don't include files from MPMII_1.img
    --no-disk2      Don't include files from MPMII_2.img
    --empty         Create empty formatted disk only

The hd1k format provides:
    - 8 MB capacity
    - 1024 directory entries
    - 512 bytes/sector, 16 sectors/track, 1024 tracks
    - No sector skew

Environment:
    DISKDEFS        Path to RomWBW diskdefs file for cpmtools
                    Default: \$HOME/esrc/RomWBW-v3.5.1/Tools/cpmtools/diskdefs

Example:
    $0                          # Create default disk
    $0 -o my_disk.img           # Create custom output
    $0 --empty -o blank.img     # Create empty formatted disk

EOF
    exit 0
}

# Parse arguments
OUTPUT="$OUTPUT_DIR/mpm2_hd1k.img"
INCLUDE_DISK1=1
INCLUDE_DISK2=1
EMPTY_ONLY=0

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            ;;
        -d|--disks)
            MPM2_DISKS="$2"
            shift 2
            ;;
        -o|--output)
            OUTPUT="$2"
            shift 2
            ;;
        --no-disk1)
            INCLUDE_DISK1=0
            shift
            ;;
        --no-disk2)
            INCLUDE_DISK2=0
            shift
            ;;
        --empty)
            EMPTY_ONLY=1
            shift
            ;;
        -*)
            echo "Unknown option: $1"
            exit 1
            ;;
        *)
            OUTPUT="$1"
            shift
            ;;
    esac
done

# Run
echo "MP/M II hd1k Disk Builder"
echo "========================="
echo ""

check_tools

# Create output directory
mkdir -p "$(dirname "$OUTPUT")"

# Create the disk
create_hd1k "$OUTPUT"

if [ $EMPTY_ONLY -eq 1 ]; then
    echo ""
    echo "Empty disk created: $OUTPUT"
    exit 0
fi

# Extract and copy files
setup_temp

if [ $INCLUDE_DISK1 -eq 1 ]; then
    extract_files "$MPM2_DISKS/MPMII_1.img" "$TEMP_DIR"
fi

if [ $INCLUDE_DISK2 -eq 1 ]; then
    extract_files "$MPM2_DISKS/MPMII_2.img" "$TEMP_DIR"
fi

# Copy all extracted files to hd1k
copy_to_hd1k "$OUTPUT" "$TEMP_DIR"

# Show results
show_contents "$OUTPUT"

echo ""
echo "Disk image created: $OUTPUT"
echo ""
echo "To use with the emulator:"
echo "  ./mpm2_emu -d A:$OUTPUT -b mpm2boot.bin"
