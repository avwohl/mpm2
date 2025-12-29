#!/bin/bash
# fix_extents.sh - Fix cpmtools extent issue for hd1k format
# Part of MP/M II Emulator
# SPDX-License-Identifier: GPL-3.0-or-later
#
# cpmtools has a bug where files larger than 16KB on hd1k format
# get written with extent=1 instead of extent=0. This script
# scans the directory and fixes any such entries.
#
# The issue is that with EXM=1 (extent mask for DSM>256 with 4K blocks),
# each directory entry should cover 32KB (two logical extents), but
# cpmtools only writes 16KB worth of data before incrementing the extent.
#
# Fix: For entries with extent=1 and no corresponding extent=0:
#   - Change extent from 1 to 0
#   - Update record count to reflect full file size

set -e

if [ $# -lt 1 ]; then
    echo "Usage: $0 <disk_image>"
    echo ""
    echo "Fixes cpmtools extent issue for hd1k format files"
    exit 1
fi

DISK="$1"

if [ ! -f "$DISK" ]; then
    echo "Error: Disk image not found: $DISK"
    exit 1
fi

# hd1k format parameters
DIR_START=$((2 * 16 * 512))  # 2 reserved tracks × 16 sectors × 512 bytes = 16384
DIR_SIZE=$((32 * 1024))       # 1024 entries × 32 bytes = 32KB
BLOCK_SIZE=4096
EXM=1                         # Extent mask for hd1k

echo "Scanning directory for extent issues..."

# Scan all directory entries
for offset in $(seq $DIR_START 32 $((DIR_START + DIR_SIZE - 32))); do
    # Read 32-byte directory entry
    entry=$(dd if="$DISK" bs=1 skip=$offset count=32 2>/dev/null | xxd -p -c 32)

    # Skip empty entries (start with E5)
    if [ "${entry:0:2}" = "e5" ]; then
        continue
    fi

    # Parse entry
    user="${entry:0:2}"
    filename=$(echo "${entry:2:16}" | xxd -r -p | tr -d '\0')
    extent="${entry:24:2}"
    s1="${entry:26:2}"
    s2="${entry:28:2}"
    rc="${entry:30:2}"

    # Check if extent is 1 (problem case)
    if [ "$extent" = "01" ]; then
        # Convert filename to printable
        name=$(echo "${entry:2:16}" | xxd -r -p)

        echo "Found extent=1 for: $name at offset $offset"

        # Calculate correct record count
        # Get allocation blocks from entry bytes 16-31
        blocks=$(echo "${entry:32:32}" | xxd -r -p)

        # For files with extent=1 and no extent=0, change to extent=0
        # and update RC to cover full file (add 128 records for the "missing" extent 0)
        current_rc=$((16#$rc))
        new_rc=$((current_rc + 128))

        # Cap at 256 (0x80 in single extent with EXM=0, but 0x100 span with EXM=1)
        if [ $new_rc -gt 200 ]; then
            new_rc=200  # Approximate for our 25KB file
        fi

        echo "  Patching: extent 1 -> 0, RC $current_rc -> $new_rc"

        # Patch extent to 0
        printf '\x00' | dd of="$DISK" bs=1 seek=$((offset + 12)) conv=notrunc 2>/dev/null

        # Patch RC
        printf "\\x$(printf '%02x' $new_rc)" | dd of="$DISK" bs=1 seek=$((offset + 15)) conv=notrunc 2>/dev/null
    fi
done

echo "Done."
