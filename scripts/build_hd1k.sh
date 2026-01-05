#!/bin/bash
set -o errexit
#set -o verbose
#set -o xtrace

# build_hd1k.sh - Create an hd1k disk image with MP/M II files
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The hd1k format (from RomWBW) provides:
# - 8 MB capacity (8,388,608 bytes)
# - 1024 directory entries (vs 64 on old floppy formats)
# - 512 bytes/sector, 16 sectors/track, 1024 tracks
# - No sector skew needed (skew 0)
#
# Uses cpm_disk.py for all disk operations to properly set SYS attributes

set -e

# Configuration
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
MPM2_DISKS="$PROJECT_DIR/mpm2_external/mpm2disks"
OUTPUT_DIR="$PROJECT_DIR/disks"
CPM_DISK="${CPM_DISK:-$HOME/src/cpmemu/util/cpm_disk.py}"
TEMP_DIR=""

# Binary tree selection (dri or src)
TREE="dri"
BIN_DIR=""  # Set after parsing args

# Check for required tools
check_tools() {
    if [ ! -f "$CPM_DISK" ]; then
        echo "Error: cpm_disk.py not found at $CPM_DISK"
        echo "Set CPM_DISK environment variable to cpm_disk.py path"
        exit 1
    fi
}

# Create a temporary directory for file extraction
setup_temp() {
    TEMP_DIR=$(mktemp -d)
    trap "rm -rf '$TEMP_DIR'" EXIT
}

# Get list of files from SSSD disk image
list_sssd_files() {
    local image="$1"
    python3 "$CPM_DISK" list "$image" 2>/dev/null | tail -n +3 | awk '{print $2}'
}

# Extract files from an SSSD (ibm-3740) disk image
extract_files() {
    local image="$1"
    local dest="$2"

    if [ ! -f "$image" ]; then
        echo "Warning: Disk image not found: $image"
        return 1
    fi

    echo "Extracting files from $(basename "$image")..."

    # Get list of files and extract each one
    local files=$(list_sssd_files "$image")
    local count=0

    for file in $files; do
        python3 "$CPM_DISK" extract -o "$dest" "$image" "$file" 2>/dev/null && {
            count=$((count + 1))
        } || {
            echo "  Warning: Could not extract $file"
        }
    done

    echo "  Extracted $count files"
    return 0
}

# Copy files from mpm2dist directory (only files not already present)
copy_dist_files() {
    local src="$1"
    local dest="$2"

    if [ ! -d "$src" ]; then
        echo "Warning: mpm2dist directory not found: $src"
        return 1
    fi

    echo "Copying additional files from mpm2dist..."
    local count=0
    local skipped=0

    for file in "$src"/*; do
        if [ -f "$file" ]; then
            local name=$(basename "$file")
            # Skip if file already exists (from floppy images)
            if [ -f "$dest/$name" ]; then
                skipped=$((skipped + 1))
            else
                cp "$file" "$dest/"
                count=$((count + 1))
            fi
        fi
    done

    echo "  Added $count new files (skipped $skipped already present)"
    return 0
}

# Create an 8MB hd1k disk image
create_hd1k() {
    local output="$1"

    echo "Creating 8MB hd1k disk image: $output"
    python3 "$CPM_DISK" create -f "$output"
}

# Copy files to hd1k image, setting SYS attribute on .PRL files
copy_to_hd1k() {
    local image="$1"
    local src_dir="$2"

    echo "Copying files to hd1k image..."

    # Separate files into PRL (need SYS) and others
    local prl_files=""
    local other_files=""
    local prl_count=0
    local other_count=0

    for file in "$src_dir"/*; do
        if [ -f "$file" ]; then
            local name=$(basename "$file")
            local ext="${name##*.}"
            if [ "$ext" = "PRL" ] || [ "$ext" = "prl" ]; then
                prl_files="$prl_files $file"
                prl_count=$((prl_count + 1))
            else
                other_files="$other_files $file"
                other_count=$((other_count + 1))
            fi
        fi
    done

    # Add PRL files with SYS attribute
    if [ $prl_count -gt 0 ]; then
        echo "  Adding $prl_count .PRL files with SYS attribute..."
        python3 "$CPM_DISK" add --sys "$image" $prl_files
    fi

    # Add other files without SYS attribute
    if [ $other_count -gt 0 ]; then
        echo "  Adding $other_count other files..."
        python3 "$CPM_DISK" add "$image" $other_files
    fi

    echo "  Copied $((prl_count + other_count)) files total"
}

# Create startup files for each console
# MP/M II TMP looks for $n$.SUP in user n's area at login
create_startup_files() {
    local image="$1"
    local temp_file=$(mktemp)

    echo "Creating startup files for consoles 0-3..."

    # Create startup file content: "DIR\r" + 0x1A padding to 128 bytes
    printf 'DIR\r' > "$temp_file"
    dd if=/dev/zero bs=1 count=124 2>/dev/null | tr '\0' '\032' >> "$temp_file"

    # Add to each user area (console n runs as user n)
    for u in 0 1 2 3; do
        local filename="\$${u}\$.SUP"
        # Create temp file with correct name
        cp "$temp_file" "$TEMP_DIR/$filename"
        python3 "$CPM_DISK" add -u $u "$image" "$TEMP_DIR/$filename" 2>/dev/null || {
            echo "  Warning: Could not create $filename for user $u"
        }
    done

    rm -f "$temp_file"
    echo "  Created startup files: \$0\$.SUP through \$3\$.SUP"
}

# Show disk contents
show_contents() {
    local image="$1"

    echo ""
    echo "Disk contents:"
    python3 "$CPM_DISK" list "$image" | head -43
    local total=$(python3 "$CPM_DISK" list "$image" | tail -n +3 | wc -l)
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
Uses cpm_disk.py for proper SYS attribute handling on .PRL files.

Options:
    -h, --help      Show this help message
    --tree=TREE     Binary tree to use: 'dri' (default) or 'src'
    -d, --disks DIR Path to MP/M II disk images (default: mpm2_external/mpm2disks)
    -o, --output    Output image path (default: disks/mpm2_hd1k.img)
    --no-disk1      Don't include files from MPMII_1.img
    --no-disk2      Don't include files from MPMII_2.img
    --empty         Create empty formatted disk only

Binary Trees:
    dri     Use original DRI binaries from bin/dri/
    src     Use source-built binaries from bin/src/ (requires build_src.sh)

The hd1k format provides:
    - 8 MB capacity
    - 1024 directory entries
    - 512 bytes/sector, 16 sectors/track, 1024 tracks
    - No sector skew

Environment:
    CPM_DISK        Path to cpm_disk.py utility
                    Default: \$HOME/src/cpmemu/util/cpm_disk.py

Example:
    $0                          # Create disk with DRI binaries
    $0 --tree=src               # Create disk with source-built binaries
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
        --tree=*)
            TREE="${1#--tree=}"
            shift
            ;;
        --tree)
            TREE="$2"
            shift 2
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

# Validate tree selection
if [ "$TREE" != "dri" ] && [ "$TREE" != "src" ]; then
    echo "Error: Invalid tree '$TREE'. Use 'dri' or 'src'"
    exit 1
fi

# Set binary directory based on tree
BIN_DIR="$PROJECT_DIR/bin/$TREE"
DRI_BIN_DIR="$PROJECT_DIR/bin/dri"

# Run
echo "MP/M II hd1k Disk Builder"
echo "========================="
echo "Tree:   $TREE"
echo "Output: $OUTPUT"
echo ""

check_tools

# Check that binary directory exists
if [ ! -d "$BIN_DIR" ]; then
    echo "Error: Binary directory not found: $BIN_DIR"
    if [ "$TREE" = "src" ]; then
        echo "Run scripts/build_src.sh first to build from source"
    fi
    exit 1
fi

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

# Copy files from selected binary tree
echo "Copying files from $TREE tree..."
cp "$BIN_DIR"/* "$TEMP_DIR/" 2>/dev/null || true
BIN_COUNT=$(ls "$TEMP_DIR" 2>/dev/null | wc -l | tr -d ' ')
echo "  Copied $BIN_COUNT files from bin/$TREE/"

# For source tree, also add static files from DRI that aren't built
# (like .LIB macro libraries, .DOC documentation, sample .ASM files)
if [ "$TREE" = "src" ]; then
    echo "Adding static files from DRI (libraries, docs, samples)..."
    STATIC_COUNT=0
    for ext in LIB DOC; do
        for f in "$DRI_BIN_DIR"/*.$ext; do
            if [ -f "$f" ]; then
                name=$(basename "$f")
                if [ ! -f "$TEMP_DIR/$name" ]; then
                    cp "$f" "$TEMP_DIR/"
                    STATIC_COUNT=$((STATIC_COUNT + 1))
                fi
            fi
        done
    done
    # Also add sample ASM files (BOOT.ASM, LDRBIOS.ASM, etc.)
    for f in "$DRI_BIN_DIR"/*.ASM; do
        if [ -f "$f" ]; then
            name=$(basename "$f")
            if [ ! -f "$TEMP_DIR/$name" ]; then
                cp "$f" "$TEMP_DIR/"
                STATIC_COUNT=$((STATIC_COUNT + 1))
            fi
        fi
    done
    echo "  Added $STATIC_COUNT static files"
fi

# Optionally add files from original floppy images (for completeness)
if [ $INCLUDE_DISK1 -eq 1 ] && [ -f "$MPM2_DISKS/MPMII_1.img" ]; then
    extract_files "$MPM2_DISKS/MPMII_1.img" "$TEMP_DIR"
fi

if [ $INCLUDE_DISK2 -eq 1 ] && [ -f "$MPM2_DISKS/MPMII_2.img" ]; then
    extract_files "$MPM2_DISKS/MPMII_2.img" "$TEMP_DIR"
fi

# Copy all files to hd1k
copy_to_hd1k "$OUTPUT" "$TEMP_DIR"

# Create startup files for each console
# DISABLED: SUP files cause "Bad Sector" error and break input handling
# TODO: Investigate why SUP files cause issues
# create_startup_files "$OUTPUT"

# Show results
show_contents "$OUTPUT"

echo ""
echo "Disk image created: $OUTPUT"
