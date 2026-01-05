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

# Build SFTP.BRS (banked module) - PL/M version with glue code
echo ""
echo "Building SFTP.BRS (banked module)..."

# Compile PL/M to assembly
uplm80 -m bare -O2 -o sftp_brs.mac sftp_brs.plm
echo "  Compiled sftp_brs.plm -> sftp_brs.mac"

# Assemble PL/M output and glue code
um80 -o sftp_brs.rel sftp_brs.mac
um80 -o sftp_glue.rel sftp_glue.asm
um80 -o sftp_brs_header.rel sftp_brs_header.asm
echo "  Assembled to .rel files"

# Link all together (header first for proper layout)
ul80 --prl -p 0 -s -o SFTP.BRS sftp_brs_header.rel sftp_brs.rel sftp_glue.rel

echo "  Output: SFTP.BRS"
ls -la SFTP.BRS
echo ""

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
