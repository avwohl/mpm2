#!/usr/bin/env python3
"""
mkmpm.py - Create MPM.SYS from SPR files

MPM.SYS format:
- 256-byte header (2 records) with configuration
- System code loaded from top of memory downward
- Each SPR file contributes code at its target address

For bank-switched MP/M II with common base at 8000H:
- Common memory: 8000H-FFFFH (shared)
- MPM.SYS contains all modules for Bank 0 (system bank)
"""

import struct
import sys
import os

def read_spr(filename, target_addr=None):
    """Read an SPR file and return (base_addr, code)

    SPR file format:
    - 256-byte header
    - Bytes 0-1: base address (0 = relocatable)
    - Bytes 2-3: code length in PAGES (256 bytes each)
    - After header: code bytes
    - After code: relocation bitmap (1 bit per byte of code)

    If target_addr is specified and different from base, relocate the code.
    """
    with open(filename, 'rb') as f:
        data = f.read()

    # SPR header
    base = struct.unpack('<H', data[0:2])[0]
    code_pages = struct.unpack('<H', data[2:4])[0]
    code_len = code_pages * 256  # Convert pages to bytes

    # Code starts at offset 256
    code = bytearray(data[256:256+code_len])

    # Relocation bitmap follows the code (1 bit per code byte)
    bitmap_start = 256 + code_len
    bitmap_len = (code_len + 7) // 8  # Round up
    reloc_bitmap = data[bitmap_start:bitmap_start+bitmap_len]

    # Relocate if target_addr differs from base
    if target_addr is not None and base != 0 and base != target_addr:
        delta_pages = (target_addr - base) // 256  # Page difference
        delta = target_addr - base  # Full byte difference

        print(f"    Relocating from {base:04X}H to {target_addr:04X}H (delta={delta:+d})")

        # Each bit in the bitmap indicates if the corresponding byte needs relocation
        # The byte is the HIGH byte of an address that needs adjustment
        reloc_count = 0
        for byte_idx in range(code_len):
            bitmap_byte = byte_idx // 8
            bitmap_bit = byte_idx % 8
            if bitmap_byte < len(reloc_bitmap):
                if reloc_bitmap[bitmap_byte] & (0x80 >> bitmap_bit):
                    # This byte is a high byte of an address - adjust it
                    old_val = code[byte_idx]
                    new_val = (old_val + delta_pages) & 0xFF
                    code[byte_idx] = new_val
                    reloc_count += 1

        print(f"    Relocated {reloc_count} address bytes")

    return base, bytes(code)

def main():
    # Paths relative to project
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.dirname(script_dir)
    disks_dir = os.path.join(project_dir, 'disks')
    dist_dir = os.path.join(project_dir, 'mpm2_external', 'mpm2dist')

    # Create header from scratch based on our memory layout
    # Header format is defined in SYSDAT.LIT
    header = bytearray(256)

    # Key header fields (based on SYSDAT.LIT):
    header[0] = 0xFF      # mem_top: top page of memory (FF00H)
    header[1] = 0x08      # nmb_cns: number of consoles (8)
    header[2] = 0x07      # brkpt_RST: breakpoint RST # (7)
    header[3] = 0xFF      # sys_call_stks: add user stacks (true)
    header[4] = 0xFF      # bank_switched: bank switched (true)
    header[5] = 0xFF      # z80_cpu: Z80 version (true)
    header[6] = 0xFF      # banked_bdos: banked bdos (true)
    header[7] = 0xFC      # xios_jmp_tbl_base: XIOS at FC00H
    header[8] = 0xF0      # resbdos_base: RESBDOS at F000H
    # bytes 9-10: used by CP/NET
    header[11] = 0xCE     # xdos_base: XDOS at CE00H
    header[12] = 0xCE     # rsp_base: RSP base (same as XDOS, no RSPs)
    header[13] = 0xCD     # bnkxios_base: BNKXIOS at CD00H
    header[14] = 0xAA     # bnkbdos_base: BNKBDOS at AA00H
    header[15] = 0x02     # max_mem_seg: max memory segment number

    # Bytes 16-47: Memory segment table (8 segments × 4 bytes each)
    # Format: base_page, size_pages, attrib, bank
    # Segment 0: bank 0 user memory (0000-7FFF = 128 pages)
    header[16] = 0x00; header[17] = 0x80; header[18] = 0x00; header[19] = 0x00
    # Segment 1: bank 1 user memory (0000-7FFF = 128 pages)
    header[20] = 0x00; header[21] = 0x80; header[22] = 0x00; header[23] = 0x01

    # Byte 122: ticks_per_second
    header[122] = 60      # 60 Hz tick rate

    # Byte 123: system_drive
    header[123] = 0x00    # Drive A:

    # Byte 124: common_base
    header[124] = 0x80    # Common memory starts at 8000H

    # Byte 125: nmb_rsps
    header[125] = 0x00    # No RSPs

    # Copyright message at bytes 144-180 (37 bytes)
    # Must match MPMLDR's internal data: 'COPYRIGHT (C) 1981, DIGITAL RESEARCH ' + serial
    copyright_msg = b'COPYRIGHT (C) 1981, DIGITAL RESEARCH '
    header[144:144+len(copyright_msg)] = copyright_msg

    # Serial number at bytes 181-186 (6 bytes)
    # Must match the serial in MPMLDR.COM: 00 14 01 00 09 2F
    # All 6 bytes need to match since company_name is 18 bytes, and we need 24 to succeed
    header[181:187] = bytes([0x00, 0x14, 0x01, 0x00, 0x09, 0x2F])

    # Bytes 187-188: max locked records/process, max open files/process
    header[187] = 0x14    # max_locked_records = 20
    header[188] = 0x09    # max_open_files = 9

    # Bytes 241-249: Additional base addresses
    header[241] = 0xCE    # cmnxdos_base (same as xdos_base for non-banked xdos)
    header[242] = 0xA8    # bnkxdos_base: BNKXDOS at A800H
    header[243] = 0xFE    # tmpd_base: TMPD at FE00H
    header[244] = 0x9D    # console_dat_base: CONSOLE.DAT at 9D00H
    header[247] = 0xA4    # tmp_base: TMP at A400H

    # Memory layout from GENSYS output:
    # FF00H: SYSTEM.DAT (256 bytes)
    # FE00H: TMPD.DAT (256 bytes)
    # FD00H: USERSYS.STK (256 bytes)
    # FC00H: XIOSJMP.TBL (256 bytes) - our emulator XIOS
    # F000H: RESBDOS.SPR (3072 bytes = 0x0C00)
    # CE00H: XDOS.SPR (8704 bytes = 0x2200)
    # CD00H: BNKXIOS.SPR (256 bytes = 0x0100)
    # AA00H: BNKBDOS.SPR (8960 bytes = 0x2300)
    # A800H: BNKXDOS.SPR (512 bytes = 0x0200)
    # A400H: TMP.SPR (1024 bytes = 0x0400)
    # A100H: LCKLSTS.DAT (768 bytes = 0x0300)
    # 9D00H: CONSOLE.DAT (1024 bytes = 0x0400)

    # System starts at 9D00H and ends at FFFFH
    sys_base = 0x9D00
    sys_top = 0xFFFF
    sys_size = sys_top - sys_base + 1  # 0x6300 = 25344 bytes

    # Create memory image (initialized to 0)
    mem = bytearray(sys_size)

    # Helper to place data at an address
    def place(addr, data, name="data"):
        offset = addr - sys_base
        if offset < 0 or offset + len(data) > len(mem):
            print(f"Warning: {name} at {addr:04X} outside range")
            return
        mem[offset:offset+len(data)] = data
        print(f"  {name}: {addr:04X}H size={len(data):04X}H ({len(data)} bytes)")

    print("Building MPM.SYS...")
    print(f"  System range: {sys_base:04X}H - {sys_top:04X}H ({sys_size} bytes)")
    print()

    # Load SPR files
    spr_files = [
        ('RESBDOS.SPR', 0xF000, dist_dir),
        ('XDOS.SPR', 0xCE00, dist_dir),
        # BNKXIOS is generated below, not loaded from SPR
        ('BNKBDOS.SPR', 0xAA00, dist_dir),
        ('BNKXDOS.SPR', 0xA800, dist_dir),
        ('TMP.SPR', 0xA400, dist_dir),
    ]

    print("Loading SPR files:")
    for spr_name, target_addr, spr_dir in spr_files:
        spr_path = os.path.join(spr_dir, spr_name)
        if not os.path.exists(spr_path):
            print(f"  ERROR: {spr_path} not found!")
            continue

        base, code = read_spr(spr_path, target_addr)  # Pass target for relocation
        # Use target address, not the SPR's internal base
        place(target_addr, code, spr_name)

    # Generate BNKXIOS code directly - it's just JP instructions forwarding to 8800H
    # BNKXIOS at CD00H forwards BIOS calls to the emulator XIOS at 8800H
    print()
    print("Generating BNKXIOS forwarding stubs:")
    bnkxios_code = bytearray()
    xios_base = 0x8800  # Emulator XIOS address
    for i in range(30):  # 30 BIOS entry points (3 bytes each = 90 bytes)
        target = xios_base + i * 3
        bnkxios_code.append(0xC3)  # JP
        bnkxios_code.append(target & 0xFF)  # Low byte
        bnkxios_code.append((target >> 8) & 0xFF)  # High byte
    # Pad to 256 bytes
    while len(bnkxios_code) < 256:
        bnkxios_code.append(0x00)
    place(0xCD00, bytes(bnkxios_code), "BNKXIOS (generated)")

    print()
    print("Loading DAT files (zeroed for now):")
    # DAT regions are just reserved space, initialize to 0
    dat_regions = [
        ('SYSTEM.DAT', 0xFF00, 0x0100),
        ('TMPD.DAT', 0xFE00, 0x0100),
        ('USERSYS.STK', 0xFD00, 0x0100),
        ('XIOSJMP.TBL', 0xFC00, 0x0100),
        ('LCKLSTS.DAT', 0xA100, 0x0300),
        ('CONSOLE.DAT', 0x9D00, 0x0400),
    ]
    for name, addr, size in dat_regions:
        # Already zero, just report
        print(f"  {name}: {addr:04X}H size={size:04X}H")

    # FC00H (XIOSJMP.TBL) should contain RET (0xC9) instructions, not zeros
    # The emulator intercepts at FC00+offset BEFORE executing any instruction
    # If code accidentally ends up here (e.g., due to bad return address),
    # RET will return safely instead of sliding through NOPs to FF00 (RST 7)
    xios_offset = 0xFC00 - sys_base
    for i in range(0x100):  # Fill entire 256 bytes with RET
        mem[xios_offset + i] = 0xC9  # RET instruction
    print(f"  XIOSJMP.TBL: FC00H filled with RET (0xC9) instructions")

    # IMPORTANT: Place header data at SYSTEM.DAT location (FF00H) in memory image
    # MPMLDR loads the memory image AFTER copying the header, so the memory image
    # at FF00H overwrites whatever MPMLDR put there. We must include the header
    # in the memory image at offset 0x6200 (FF00H - 9D00H).
    print()
    print("Placing header at SYSTEM.DAT location (FF00H):")
    sysdat_offset = 0xFF00 - sys_base  # 0x6200
    # Copy header to memory image at FF00H
    mem[sysdat_offset:sysdat_offset+256] = header
    print(f"  SYSTEM.DAT: FF00H (copied header, {len(header)} bytes)")

    # Calculate number of 128-byte records
    num_records = (sys_size + 127) // 128  # Round up
    print()
    print(f"Total records: {num_records} ({num_records * 128} bytes)")

    # Update header with correct record count at bytes 120-121
    header_ba = bytearray(header)
    header_ba[120] = num_records & 0xFF
    header_ba[121] = (num_records >> 8) & 0xFF
    # Note: bytes 122=ticks_per_second(60), 123=system_drive(0) are set earlier

    # Write output
    output_path = os.path.join(disks_dir, 'MPM_complete.SYS')
    with open(output_path, 'wb') as f:
        f.write(header_ba)  # 256-byte header
        f.write(mem)        # System memory image

    print()
    print(f"Output: {output_path}")
    print(f"Size: {256 + len(mem)} bytes")

    # Also copy to MPM.SYS
    mpm_dest = os.path.join(disks_dir, 'MPM.SYS')
    with open(mpm_dest, 'wb') as f:
        f.write(header_ba)
        f.write(mem)
    print(f"Copied to: {mpm_dest}")

if __name__ == '__main__':
    main()
