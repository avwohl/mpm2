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

# Build assembly files using um80/ul80
echo ""
echo "Assembling LDRBIOS..."
cd "$ASM_DIR"

# Assemble LDRBIOS
um80 -o ldrbios.rel ldrbios.asm
ul80 --hex -o ldrbios.hex ldrbios.rel

# Convert Intel HEX to binary
python3 -c "
data = {}
with open('ldrbios.hex', 'r') as f:
    for line in f:
        if not line.startswith(':'): continue
        count = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rtype = int(line[7:9], 16)
        if rtype != 0: continue
        for i in range(count):
            data[addr + i] = int(line[9 + i*2:11 + i*2], 16)
if data:
    min_addr = min(data.keys())
    max_addr = max(data.keys())
    out = bytearray(max_addr - min_addr + 1)
    for addr, val in data.items():
        out[addr - min_addr] = val
    with open('ldrbios.bin', 'wb') as f:
        f.write(out)
"

# Assemble XIOS
um80 -o xios_port.rel xios_port.asm
ul80 --hex -o xios_port.hex xios_port.rel

# Convert to binary
python3 -c "
data = {}
with open('xios_port.hex', 'r') as f:
    for line in f:
        if not line.startswith(':'): continue
        count = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        rtype = int(line[7:9], 16)
        if rtype != 0: continue
        for i in range(count):
            data[addr + i] = int(line[9 + i*2:11 + i*2], 16)
if data:
    min_addr = min(data.keys())
    max_addr = max(data.keys())
    out = bytearray(max_addr - min_addr + 1)
    for addr, val in data.items():
        out[addr - min_addr] = val
    with open('xios.bin', 'wb') as f:
        f.write(out)
"

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
