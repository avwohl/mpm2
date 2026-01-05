#!/bin/bash
# Build SFTP RSP modules for MP/M II
# Creates SFTP.RSP (common memory) and SFTP.BRS (bank 0)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ASM_DIR="$PROJECT_DIR/asm"

cd "$ASM_DIR"

echo "=============================================="
echo "Building SFTP RSP Modules"
echo "=============================================="
echo ""

# Build SFTP.RSP (common module)
echo "Building SFTP.RSP (common module)..."
uplm80 -m bare -O2 -o sftp_rsp.mac sftp_rsp.plm
um80 -o sftp_rsp.rel sftp_rsp.mac
ul80 --prl -p 0 -o SFTP.RSP sftp_rsp.rel
echo "  Output: SFTP.RSP"
ls -la SFTP.RSP
echo ""

# Build assembly glue
echo "Building assembly glue..."
um80 -o sftp_glue.rel sftp_glue.asm
echo "Building BRS header..."
um80 -o sftp_brs_header.rel sftp_brs_header.asm

# Build PL/M code
echo ""
echo "Building SFTP.BRS (banked module)..."
uplm80 -m bare -O2 -o sftp_brs.mac sftp_brs.plm

# Patch generated assembly to make SFTPMAIN public
sed -i 's/^SFTPMAIN:/        PUBLIC  SFTPMAIN\nSFTPMAIN:/' sftp_brs.mac

# Assemble to relocatable object
um80 -o sftp_brs.rel sftp_brs.mac

# Link modules with header FIRST for correct offsets
ul80 --prl -p 0 -s -o SFTP.BRS sftp_brs_header.rel sftp_brs.rel sftp_glue.rel

# Show the relocation bitmap (use ul80's original, don't patch)
# ul80 generates: 10 02 10 00 ... which matches MPMSTAT.BRS pattern
python3 << 'SHOW_BITMAP'
with open('SFTP.BRS', 'rb') as f:
    data = bytearray(f.read())
    code_size = data[1] | (data[2] << 8)
    bitmap_offset = 256 + code_size
    print(f"  Code size: 0x{code_size:04X} bytes")
    print(f"  Bitmap at offset: 0x{bitmap_offset:04X}")
    print(f"  Bitmap bytes 0-7: {' '.join(f'{data[bitmap_offset+i]:02X}' for i in range(8))}")
SHOW_BITMAP

echo "  Output: SFTP.BRS"
ls -la SFTP.BRS
echo ""

# Clear header bytes 4-5 (MPMSTAT.BRS has 00 00, ul80 puts non-zero value)
python3 << 'HEADER_PATCH'
for f in ['SFTP.RSP', 'SFTP.BRS']:
    with open(f, 'r+b') as fp:
        data = bytearray(fp.read())
        data[4] = 0x00
        data[5] = 0x00
        fp.seek(0)
        fp.write(data)
        print(f"Cleared header bytes 4-5: {f}")
HEADER_PATCH

echo ""
echo "=============================================="
echo "SFTP RSP Build Complete"
echo "=============================================="
echo ""
echo "Files created:"
echo "  Common module: $ASM_DIR/SFTP.RSP"
echo "  Banked module: $ASM_DIR/SFTP.BRS"
echo ""
echo "Next steps:"
echo "  1. Run ./scripts/build_hd1k.sh to create disk image"
echo "  2. Run ./scripts/gensys.sh to include SFTP RSP in MPM.SYS"
