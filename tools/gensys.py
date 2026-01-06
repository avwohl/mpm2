#!/usr/bin/env python3
"""
gensys.py - MP/M II System Generator (Python replacement for DRI GENSYS)

Creates MPM.SYS by loading and relocating SPR modules.

This replaces the original DRI GENSYS.COM which has bugs in PRL relocation
for files larger than 1024 bytes.

Usage:
    gensys.py config.json [--output mpm.sys]
    gensys.py --help

SPDX-License-Identifier: GPL-3.0-or-later
"""

import argparse
import json
import struct
import sys
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Tuple


@dataclass
class SPRModule:
    """Represents a loaded SPR/PRL module."""
    name: str
    path: Path
    code: bytearray
    psize: int  # Program/code size
    dsize: int  # Data/buffer size
    base: int = 0  # Load address (set during relocation)

    @classmethod
    def load(cls, path: Path) -> 'SPRModule':
        """Load an SPR file and extract its header."""
        data = path.read_bytes()
        if len(data) < 256:
            raise ValueError(f"{path}: File too small for SPR format")

        # SPR header format (first 256 bytes):
        # Byte 0: fill
        # Bytes 1-2: psize (program size in bytes, little-endian)
        # Byte 3: fill
        # Bytes 4-5: dsize (data size in bytes, little-endian)
        # Bytes 6-127: fill/reserved
        # Bytes 128-255: First 128 bytes of relocation bitmap

        psize = struct.unpack('<H', data[1:3])[0]
        dsize = struct.unpack('<H', data[4:6])[0]

        # Code starts at byte 256
        code_start = 256
        code_end = code_start + psize

        if len(data) < code_end:
            raise ValueError(f"{path}: File truncated, expected {code_end} bytes, got {len(data)}")

        code = bytearray(data[code_start:code_end])

        return cls(
            name=path.stem.upper(),
            path=path,
            code=code,
            psize=psize,
            dsize=dsize
        )

    def get_bitmap(self) -> bytes:
        """Extract the relocation bitmap from the SPR file.

        PRL/SPR file structure:
        - Bytes 0-255: Header (bytes 1-2 contain code length)
        - Bytes 256+: Code (code_length bytes)
        - After code: Relocation bitmap ((code_length + 7) // 8 bytes)

        The bitmap has one bit per code byte, MSB-first order.
        Bit 7 of bitmap byte 0 = code byte 0, etc.
        """
        data = self.path.read_bytes()

        # Calculate how many bitmap bytes we need
        bitmap_bytes_needed = (self.psize + 7) // 8

        # Bitmap starts immediately after code
        bitmap_start = 256 + self.psize
        return data[bitmap_start:bitmap_start + bitmap_bytes_needed]

    def relocate(self, base_page: int) -> None:
        """Relocate the module to the given base page address.

        Uses the relocation bitmap from the SPR file. Each bit in the bitmap
        indicates whether the corresponding code byte is a high byte of a
        16-bit address that needs the base page added.

        The bitmap uses MSB-first bit ordering: bit 7 of byte 0 corresponds
        to code byte 0, bit 6 to code byte 1, etc.
        """
        self.base = base_page * 256
        bitmap = self.get_bitmap()

        # Apply bitmap-based relocation
        for byte_idx in range(self.psize):
            bitmap_byte = byte_idx // 8
            bitmap_bit = 7 - (byte_idx % 8)  # MSB-first ordering

            if bitmap_byte < len(bitmap):
                if bitmap[bitmap_byte] & (1 << bitmap_bit):
                    # This byte needs relocation - add base page
                    self.code[byte_idx] = (self.code[byte_idx] + base_page) & 0xFF

    def _relocate_by_instruction(self, base_page: int, already_relocated: set = None) -> None:
        """Relocate by scanning for instructions with 16-bit addresses.

        Z80 instructions that have 16-bit absolute addresses:
        - C3 nn nn: JP nn (unconditional jump)
        - CD nn nn: CALL nn (call subroutine)
        - C2/CA/D2/DA/E2/EA/F2/FA nn nn: JP cc,nn (conditional jumps)
        - C4/CC/D4/DC/E4/EC/F4/FC nn nn: CALL cc,nn (conditional calls)
        - 21 nn nn: LD HL,nn
        - 11 nn nn: LD DE,nn
        - 01 nn nn: LD BC,nn
        - 31 nn nn: LD SP,nn
        - 3A nn nn: LD A,(nn)
        - 32 nn nn: LD (nn),A
        - 2A nn nn: LD HL,(nn)
        - 22 nn nn: LD (nn),HL
        - ED 4B nn nn: LD BC,(nn)
        - ED 5B nn nn: LD DE,(nn)
        - ED 7B nn nn: LD SP,(nn)
        - ED 43 nn nn: LD (nn),BC
        - ED 53 nn nn: LD (nn),DE
        - ED 73 nn nn: LD (nn),SP

        Args:
            base_page: The base page to add to addresses
            already_relocated: Set of byte indices already relocated by bitmap
        """
        if already_relocated is None:
            already_relocated = set()

        i = 0
        while i < self.psize - 2:
            opcode = self.code[i]

            # Check for instructions with absolute addresses
            # The address high byte needs relocation if it's within module range
            addr_offset = -1  # Offset of high byte from current position

            # JP nn, CALL nn
            if opcode in (0xC3, 0xCD):
                addr_offset = 2
            # Conditional JP cc,nn
            elif opcode in (0xC2, 0xCA, 0xD2, 0xDA, 0xE2, 0xEA, 0xF2, 0xFA):
                addr_offset = 2
            # Conditional CALL cc,nn
            elif opcode in (0xC4, 0xCC, 0xD4, 0xDC, 0xE4, 0xEC, 0xF4, 0xFC):
                addr_offset = 2
            # LD rr,nn (16-bit immediate)
            elif opcode in (0x01, 0x11, 0x21, 0x31):
                addr_offset = 2
            # LD A,(nn), LD (nn),A
            elif opcode in (0x3A, 0x32):
                addr_offset = 2
            # LD HL,(nn), LD (nn),HL
            elif opcode in (0x2A, 0x22):
                addr_offset = 2
            # ED prefix instructions
            elif opcode == 0xED and i < self.psize - 3:
                ed_opcode = self.code[i + 1]
                # LD rr,(nn), LD (nn),rr
                if ed_opcode in (0x4B, 0x5B, 0x7B, 0x43, 0x53, 0x73):
                    addr_offset = 3
                    i += 1  # Skip the ED prefix

            if addr_offset > 0 and i + addr_offset < self.psize:
                high_byte_idx = i + addr_offset

                # Skip if already relocated by bitmap
                if high_byte_idx not in already_relocated:
                    # Get the address from the instruction
                    lo = self.code[i + addr_offset - 1]
                    hi = self.code[high_byte_idx]

                    # Only relocate if address looks like it's within this module
                    # (high byte is less than base_page, meaning it's relative to page 0)
                    if hi < base_page:
                        # Add base_page to high byte
                        self.code[high_byte_idx] = (hi + base_page) & 0xFF

                i += addr_offset + 1
            else:
                i += 1


@dataclass
class RSPModule:
    """Represents an RSP (Resident System Process) module."""
    name: str
    rsp_path: Path  # .RSP file (common memory)
    brs_path: Optional[Path] = None  # .BRS file (bank 0, optional)
    rsp: Optional[SPRModule] = None
    brs: Optional[SPRModule] = None
    rsp_record: int = 0  # Record number in MPM.SYS
    brs_record: int = 0
    rsp_base: int = 0  # RSP load address (set during relocation)

    def load(self) -> None:
        """Load RSP and optional BRS modules."""
        self.rsp = SPRModule.load(self.rsp_path)
        if self.brs_path and self.brs_path.exists():
            self.brs = SPRModule.load(self.brs_path)


@dataclass
class SystemConfig:
    """MP/M II system configuration."""
    # Memory configuration
    mem_top: int = 0xFF  # Top page of memory
    common_base: int = 0xC0  # Common memory base page

    # Console configuration
    num_consoles: int = 4
    num_printers: int = 1

    # System options
    bank_switched: bool = True
    z80_cpu: bool = True
    sys_call_stks: bool = True
    banked_bdos: bool = True
    day_file: bool = True

    # Timing
    ticks_per_second: int = 60

    # Drives
    system_drive: int = 1  # A=1, B=2, etc.
    temp_file_drive: int = 1

    # File/record limits
    max_locked_records: int = 16
    total_locked_records: int = 32
    max_open_files: int = 16
    total_open_files: int = 32

    # Memory segments (base, size, attrib, bank) - up to 8
    num_mem_segments: int = 7

    # Breakpoint
    breakpoint_rst: int = 6

    # Input files
    spr_dir: str = "."
    resbdos_spr: str = "resbdos.spr"
    xdos_spr: str = "xdos.spr"
    bnkxios_spr: str = "bnkxios.spr"
    bnkbdos_spr: str = "bnkbdos.spr"
    bnkxdos_spr: str = "bnkxdos.spr"
    tmp_spr: str = "tmp.spr"

    # RSPs to include
    rsps: List[Dict] = field(default_factory=list)

    # Output
    output: str = "mpm.sys"
    system_dat: str = "system.dat"

    @classmethod
    def from_json(cls, path: Path) -> 'SystemConfig':
        """Load configuration from JSON file."""
        with open(path) as f:
            data = json.load(f)

        config = cls()
        for key, value in data.items():
            if hasattr(config, key):
                setattr(config, key, value)

        return config

    def to_json(self, path: Path) -> None:
        """Save configuration to JSON file."""
        data = {
            'mem_top': self.mem_top,
            'common_base': self.common_base,
            'num_consoles': self.num_consoles,
            'num_printers': self.num_printers,
            'bank_switched': self.bank_switched,
            'z80_cpu': self.z80_cpu,
            'sys_call_stks': self.sys_call_stks,
            'banked_bdos': self.banked_bdos,
            'day_file': self.day_file,
            'ticks_per_second': self.ticks_per_second,
            'system_drive': self.system_drive,
            'temp_file_drive': self.temp_file_drive,
            'max_locked_records': self.max_locked_records,
            'total_locked_records': self.total_locked_records,
            'max_open_files': self.max_open_files,
            'total_open_files': self.total_open_files,
            'num_mem_segments': self.num_mem_segments,
            'breakpoint_rst': self.breakpoint_rst,
            'spr_dir': self.spr_dir,
            'resbdos_spr': self.resbdos_spr,
            'xdos_spr': self.xdos_spr,
            'bnkxios_spr': self.bnkxios_spr,
            'bnkbdos_spr': self.bnkbdos_spr,
            'bnkxdos_spr': self.bnkxdos_spr,
            'tmp_spr': self.tmp_spr,
            'rsps': self.rsps,
            'output': self.output,
            'system_dat': self.system_dat,
        }
        with open(path, 'w') as f:
            json.dump(data, f, indent=2)


class SystemGenerator:
    """Generates MPM.SYS from SPR modules."""

    # Copyright string offset in RESBDOS.SPR code section
    # RESBDOS has " COPYRIGHT (C) 1981, DIGITAL RESEARCH " with leading space
    # We skip the leading space to get 37 chars
    COPYRIGHT_OFFSET = 0xD4  # Skip leading space at 0xD3
    COPYRIGHT_LEN = 37  # "COPYRIGHT (C) 1981, DIGITAL RESEARCH" (no trailing space)
    SERIAL_OFFSET = 0xF9  # Serial: 6 bytes starting at 0xF9 in code
    SERIAL_LEN = 6

    def __init__(self, config: SystemConfig):
        self.config = config
        self.spr_dir = Path(config.spr_dir)

        # Loaded modules
        self.resbdos: Optional[SPRModule] = None
        self.xdos: Optional[SPRModule] = None
        self.bnkxios: Optional[SPRModule] = None
        self.bnkbdos: Optional[SPRModule] = None
        self.bnkxdos: Optional[SPRModule] = None
        self.tmp: Optional[SPRModule] = None
        self.rsps: List[RSPModule] = []

        # Memory layout (filled during generation)
        self.cur_base = config.mem_top  # Current base page (decreases as we add modules)
        self.xios_jmp_tbl_base = 0
        self.tmpd_base = 0
        self.console_dat_base = 0
        self.lock_free_space = 0

        # Output buffer
        self.output = bytearray()
        self.record_count = 0

        # Memory image (set during generation)
        self.mem_image = bytearray()
        self.image_base = 0

        # System data (256 bytes)
        self.system_data = bytearray(256)

        # Cross-reference data captured during loading
        self.resbdos009 = bytearray(3)  # XDOS entry
        self.resbdos012 = bytearray(6)  # XIOS common base
        self.xdos003 = bytearray(6)     # XIOS common base
        self.sysdatadr = bytearray(2)   # System data page address
        self.bnkxios000 = bytearray(256)  # XIOS jump table
        self.cmn_xdos_jmp_tbl = bytearray(40)
        self.act_xios_common_base = 0

    def load_modules(self) -> None:
        """Load all required SPR modules."""
        print("Loading SPR modules...")

        self.resbdos = SPRModule.load(self.spr_dir / self.config.resbdos_spr)
        print(f"  RESBDOS: {self.resbdos.psize} bytes code, {self.resbdos.dsize} bytes data")

        self.xdos = SPRModule.load(self.spr_dir / self.config.xdos_spr)
        print(f"  XDOS: {self.xdos.psize} bytes code, {self.xdos.dsize} bytes data")

        self.bnkxios = SPRModule.load(self.spr_dir / self.config.bnkxios_spr)
        print(f"  BNKXIOS: {self.bnkxios.psize} bytes code, {self.bnkxios.dsize} bytes data")

        self.bnkbdos = SPRModule.load(self.spr_dir / self.config.bnkbdos_spr)
        print(f"  BNKBDOS: {self.bnkbdos.psize} bytes code, {self.bnkbdos.dsize} bytes data")

        self.bnkxdos = SPRModule.load(self.spr_dir / self.config.bnkxdos_spr)
        print(f"  BNKXDOS: {self.bnkxdos.psize} bytes code, {self.bnkxdos.dsize} bytes data")

        if self.config.num_consoles > 0:
            self.tmp = SPRModule.load(self.spr_dir / self.config.tmp_spr)
            print(f"  TMP: {self.tmp.psize} bytes code, {self.tmp.dsize} bytes data")

        # Load RSPs
        for rsp_config in self.config.rsps:
            name = rsp_config.get('name', '')
            rsp_file = rsp_config.get('rsp', f'{name.lower()}.rsp')
            brs_file = rsp_config.get('brs', f'{name.lower()}.brs')

            rsp_path = self.spr_dir / rsp_file
            brs_path = self.spr_dir / brs_file

            if not rsp_path.exists():
                print(f"  Warning: RSP {rsp_path} not found, skipping")
                continue

            rsp = RSPModule(
                name=name.upper(),
                rsp_path=rsp_path,
                brs_path=brs_path if brs_path.exists() else None
            )
            rsp.load()
            self.rsps.append(rsp)

            brs_info = f", BRS: {rsp.brs.psize} bytes" if rsp.brs else ""
            print(f"  RSP {name}: {rsp.rsp.psize} bytes{brs_info}")

    def setup_system_data(self) -> None:
        """Initialize system data structure."""
        cfg = self.config

        # Fill with defaults first
        self.system_data = bytearray(256)

        # Basic configuration (offsets match SYSDAT.LIT)
        self.system_data[0] = cfg.mem_top
        self.system_data[1] = cfg.num_consoles
        self.system_data[2] = cfg.breakpoint_rst
        self.system_data[3] = 0xFF if cfg.sys_call_stks else 0
        self.system_data[4] = 0xFF if cfg.bank_switched else 0
        self.system_data[5] = 0xFF if cfg.z80_cpu else 0
        self.system_data[6] = 0xFF if cfg.banked_bdos else 0
        # Bytes 7-14 filled during module loading
        self.system_data[15] = cfg.num_mem_segments

        # Memory segment table (bytes 16-47) - filled later

        # Byte 122: ticks per second
        self.system_data[122] = cfg.ticks_per_second

        # Byte 123: system drive
        self.system_data[123] = cfg.system_drive

        # Byte 124: common base
        self.system_data[124] = cfg.common_base

        # Byte 125: number of RSPs (filled later)

        # Bytes 144-180: Copyright (37 bytes, no trailing space)
        # Read from RESBDOS.SPR code section (skip leading space)
        copyright_bytes = bytes(self.resbdos.code[self.COPYRIGHT_OFFSET:
                                                   self.COPYRIGHT_OFFSET + self.COPYRIGHT_LEN])
        self.system_data[144:144+self.COPYRIGHT_LEN] = copyright_bytes

        # Bytes 181-186: Serial number (6 bytes)
        # Read from RESBDOS.SPR code section
        serial_bytes = bytes(self.resbdos.code[self.SERIAL_OFFSET:
                                                self.SERIAL_OFFSET + self.SERIAL_LEN])
        self.system_data[181:181+self.SERIAL_LEN] = serial_bytes

        # File/record limits
        self.system_data[187] = cfg.max_locked_records
        self.system_data[188] = cfg.max_open_files
        struct.pack_into('<H', self.system_data, 189,
                        cfg.total_locked_records + cfg.total_open_files)
        self.system_data[193] = cfg.total_locked_records
        self.system_data[194] = cfg.total_open_files

        # Dayfile logging
        self.system_data[195] = 0xFF if cfg.day_file else 0

        # Drives
        self.system_data[196] = cfg.temp_file_drive
        self.system_data[197] = cfg.num_printers

    def calculate_memory_layout(self) -> None:
        """Calculate memory layout for all modules."""
        cfg = self.config

        # Start from top of memory
        self.cur_base = cfg.mem_top

        # Reserve system data page
        print(f"\n  SYSTEM  DAT  {self.cur_base:02X}00H  0100H")

        # TMPD.DAT region (if consoles > 0)
        if cfg.num_consoles > 0:
            self.cur_base -= 1
            if cfg.num_consoles > 4:
                self.cur_base -= 1
            self.tmpd_base = self.cur_base
            size = (cfg.num_consoles + 3) // 4
            print(f"  TMPD    DAT  {self.cur_base:02X}00H  {size:02X}00H")

        # User system call stacks
        usersys_stk_base = 0
        if cfg.sys_call_stks:
            self.cur_base -= 1
            if cfg.num_mem_segments > 4:
                self.cur_base -= 1
            size = (cfg.num_mem_segments + 3) // 4
            usersys_stk_base = self.cur_base
            print(f"  USERSYS STK  {self.cur_base:02X}00H  {size:02X}00H")

        # XIOS jump table
        self.cur_base -= 1
        self.xios_jmp_tbl_base = self.cur_base
        print(f"  XIOSJMP TBL  {self.cur_base:02X}00H  0100H")

        # Update system data
        self.system_data[7] = self.xios_jmp_tbl_base

        # Set up user_stacks array (offsets 80-95, 8 x 16-bit addresses)
        # DRI GENSYS formula: user_stacks(j) = tmpd_base * 256 - j * 64
        # This creates stack addresses for each memory segment
        if cfg.sys_call_stks:
            for j in range(min(cfg.num_mem_segments, 8)):
                stack_addr = self.tmpd_base * 256 - j * 64
                offset = 80 + j * 2
                struct.pack_into('<H', self.system_data, offset, stack_addr)

    def load_and_relocate_module(self, module: SPRModule, name: str) -> Tuple[int, int]:
        """Load and relocate a module, returning (base_page, size_pages)."""
        # Calculate pages needed
        total_size = module.psize + module.dsize
        pages_needed = (total_size + 255) // 256

        # Reserve space (growing downward)
        prev_base = self.cur_base
        self.cur_base -= pages_needed
        base_page = self.cur_base

        # Relocate module using standard PRL bitmap
        module.relocate(base_page)

        print(f"  {name:12s}  {base_page:02X}00H  {pages_needed:02X}00H")

        return base_page, pages_needed

    def write_module(self, module: SPRModule) -> int:
        """Write module to memory image at correct offset."""
        # Calculate offset within memory image
        # Memory image starts at self.image_base and module.base is the load address
        offset = module.base - self.image_base

        if offset < 0:
            raise ValueError(f"Module {module.name} base {module.base:04X}H below image base {self.image_base:04X}H")

        # Ensure buffer is large enough
        end_offset = offset + len(module.code)
        if end_offset > len(self.mem_image):
            raise ValueError(f"Module {module.name} extends beyond memory image")

        # Write module code to memory image
        self.mem_image[offset:offset + len(module.code)] = module.code

        # Return the record number (offset within MPM.SYS file)
        # Record 0-1 is SYSTEM.DAT, so record starts at (256 + offset) / 128
        return (256 + offset) // 128

    def generate(self) -> None:
        """Generate the complete MPM.SYS file."""
        cfg = self.config

        print("\nMP/M II System Generation (Python)")
        print("=" * 40)

        # Load modules
        self.load_modules()

        # Setup system data
        self.setup_system_data()

        # Calculate memory layout and load modules
        print("\nMemory Layout:")
        self.calculate_memory_layout()

        # Track all modules to write later
        modules_to_write: List[SPRModule] = []

        # Load and relocate RESBDOS
        base, size = self.load_and_relocate_module(self.resbdos, "RESBDOS SPR")
        self.system_data[8] = base  # resbdos_base
        self.resbdos009 = bytes(self.resbdos.code[9:12])
        self.resbdos012 = bytes(self.resbdos.code[12:18])
        modules_to_write.append(self.resbdos)

        # Load and relocate XDOS
        base, size = self.load_and_relocate_module(self.xdos, "XDOS    SPR")
        self.system_data[11] = base  # xdos_base
        # Patch XDOS with RESBDOS cross-references
        self.xdos.code[9:12] = self.resbdos009
        self.sysdatadr[0] = 0
        self.sysdatadr[1] = cfg.mem_top
        self.xdos.code[12:14] = self.sysdatadr
        self.xdos003 = bytes(self.xdos.code[3:9])
        modules_to_write.append(self.xdos)

        # Note: BDOS/XDOS entry point (bytes 245-246) set later after BNKXIOS is loaded

        # Load RSPs
        # RSPs are loaded from high to low memory (loaded later = lower address)
        # rspl points to the LOWEST address RSP (last one loaded)
        # Each RSP's pd_link (offset 0-1) points to the NEXT higher address RSP
        # The highest address RSP has pd_link = 0 (end of list)
        #
        # Since we load highest first: first RSP has pd_link=0, subsequent RSPs
        # have pd_link pointing to the previously loaded RSP
        rsp_link = 0
        brs_link = 0
        prev_rsp_addr = 0  # Address of previously loaded RSP (or 0 for first)
        for rsp in self.rsps:
            base, size = self.load_and_relocate_module(rsp.rsp, f"{rsp.name:8s}RSP")
            rsp.rsp_base = base * 256  # Store for BRS patching later

            # Patch this RSP's pd_link (offset 0-1) to point to previous RSP
            # First RSP loaded has pd_link=0 (it's the highest address, end of list)
            struct.pack_into('<H', rsp.rsp.code, 0, prev_rsp_addr)

            prev_rsp_addr = base * 256  # This RSP becomes the previous for next iteration
            rsp_link = base * 256  # Track lowest RSP for rspl

            modules_to_write.append(rsp.rsp)

        self.system_data[12] = self.cur_base  # rsp_base

        # Load and relocate BNKXIOS
        base, size = self.load_and_relocate_module(self.bnkxios, "BNKXIOS SPR")
        self.system_data[13] = base  # bnkxios_base

        # Get actual XIOS common base from module (AFTER relocation)
        # First JP instruction at offset 0 jumps to COMMONBASE
        # After relocation, bytes 1-2 contain the relocated target address
        self.act_xios_common_base = struct.unpack('<H', self.bnkxios.code[1:3])[0]

        if self.act_xios_common_base < cfg.common_base * 256:
            # For our emulator XIOS, common base might be the module base itself
            # if the entire module is in common memory
            if base >= cfg.common_base:
                self.act_xios_common_base = base * 256
                print(f"  Note: Using module base as XIOS common base: {self.act_xios_common_base:04X}H")
            else:
                raise ValueError(f"XIOS common base {self.act_xios_common_base:04X}H below "
                               f"configured common base {cfg.common_base * 256:04X}H")

        # Apply patches to BNKXIOS at COMMONBASE offset (DRI GENSYS lines 1225-1227)
        # COMMONBASE structure: JP boot, JP swtuser, JP swtsys, JP pdisp, JP xdos, DW sysdat
        # Offsets within COMMONBASE: +3=swtuser, +6=swtsys, +9=pdisp, +12=xdos, +15=sysdat
        offset = self.act_xios_common_base - base * 256
        if offset + 17 <= len(self.bnkxios.code):
            # SWTUSER and SWTSYS from RESBDOS (6 bytes = 2 JP instructions)
            # DRI GENSYS: call move (6,.resbdos012,(act$xios$common$base-cur$top)+.sctbfr(0).record(003))
            self.bnkxios.code[offset + 3:offset + 9] = self.resbdos012
            # PDISP and XDOS from XDOS (6 bytes = 2 JP instructions, already relocated)
            # DRI GENSYS: call move (6,.xdos003,(act$xios$common$base-cur$top)+.sctbfr(0).record(009))
            self.bnkxios.code[offset + 9:offset + 15] = self.xdos003
            # SYSDAT address
            # DRI GENSYS: call move (2,.sysdatadr,(act$xios$common$base-cur$top)+.sctbfr(0).record(015))
            self.bnkxios.code[offset + 15:offset + 17] = self.sysdatadr

        # Note: DRI GENSYS does NOT set bytes 245-246 (bdos_entry).
        # These may be filled in at runtime by MPMLDR or not used in MP/M 2.0.

        # Save XIOS jump table (first 256 bytes)
        self.bnkxios000 = bytes(self.bnkxios.code[:256])
        modules_to_write.append(self.bnkxios)

        # Load and relocate BNKBDOS
        base, size = self.load_and_relocate_module(self.bnkbdos, "BNKBDOS SPR")
        self.system_data[14] = base  # bnkbdos_base
        modules_to_write.append(self.bnkbdos)

        # Load and relocate BNKXDOS
        base, size = self.load_and_relocate_module(self.bnkxdos, "BNKXDOS SPR")
        # Note: DRI GENSYS does NOT set byte 241 (cmnxdos_base), leaves it as 0
        self.system_data[242] = base  # bnkxdos_base
        # Patch sysdatadr
        self.bnkxdos.code[0:2] = self.sysdatadr
        # Patch XDOS with BNKXDOS jump table at offset 14
        # Analysis of DRI GENSYS output shows this goes at the START of XDOS (offset 14),
        # not at the end. The table contains relocated BNKXDOS addresses for banked functions.
        cmn_xdos_jmp_tbl = bytes(self.bnkxdos.code[4:44])  # 40 bytes from BNKXDOS
        self.xdos.code[14:54] = cmn_xdos_jmp_tbl
        modules_to_write.append(self.bnkxdos)

        # Load TMP if needed
        if self.tmp:
            base, size = self.load_and_relocate_module(self.tmp, "TMP     SPR")
            self.system_data[243] = self.tmpd_base  # tmpd_base
            self.system_data[247] = base  # tmp_base
            self.tmp.code[0:2] = self.sysdatadr
            modules_to_write.append(self.tmp)

        # Load BRS modules for RSPs
        # GENSYS builds a linked list of BRS modules:
        # - brspl points to lowest (first loaded) BRS
        # - Each BRS's offset 2-3 contains link to next BRS (0 for last)
        # - INITSP is read from BRS offset 2-3 BEFORE overwriting with link
        brsp_base = 0
        nmb_brsps = 0
        brspl = 0  # Linked list head (address of first/lowest BRS)
        for rsp in self.rsps:
            if rsp.brs:
                base, size = self.load_and_relocate_module(rsp.brs, f"{rsp.name:8s}BRS")
                brs_base = base * 256

                # Get INITSP from BRS header (offset 2-3) AFTER relocation
                # INITSP is already the absolute stack pointer address after relocation
                # (it was a label reference that got relocated)
                stack_ptr = struct.unpack('<H', rsp.brs.code[2:4])[0]

                # Patch BRS offset 0-1 (RSPBASE) with RSP base address
                struct.pack_into('<H', rsp.brs.code, 0, rsp.rsp_base)

                # Patch BRS offset 2-3 with link to previous BRS (brspl)
                # This overwrites INITSP but we saved it above
                struct.pack_into('<H', rsp.brs.code, 2, brspl)
                brspl = brs_base  # This BRS becomes the new list head

                # Patch RSP offset 6-7 (pd_stkptr) with the saved INITSP
                # Note: pd_link at RSP offset 0-1 was already set during RSP loading
                struct.pack_into('<H', rsp.rsp.code, 6, stack_ptr)

                # Read back pd_link for debug output
                pd_link = struct.unpack('<H', rsp.rsp.code[0:2])[0]
                print(f"    RSP patches: pd_link={pd_link:04X}H, stkptr={stack_ptr:04X}H")
                # Note: brspl was already updated to brs_base above
                # The value written to BRS offset 2-3 was the OLD brspl (before update)
                old_brspl = struct.unpack('<H', rsp.brs.code[2:4])[0]  # Read back what we wrote
                print(f"    BRS patches: RSPBASE={rsp.rsp_base:04X}H, link={old_brspl:04X}H")

                modules_to_write.append(rsp.brs)
                brsp_base = base  # Base of last (lowest) BRS module
                nmb_brsps += 1

        self.system_data[248] = nmb_brsps  # nmb_brsps
        if nmb_brsps > 0:
            self.system_data[249] = brsp_base  # brsp_base
            # brspl (bytes 250-251) = address of first BRS in linked list
            struct.pack_into('<H', self.system_data, 250, brspl)

        # Calculate lock table space
        total_list_items = cfg.total_locked_records + cfg.total_open_files
        lock_pages = (total_list_items * 10 + 255) // 256
        prev_base = self.cur_base
        self.cur_base -= lock_pages
        self.lock_free_space = self.cur_base * 256
        struct.pack_into('<H', self.system_data, 191, self.lock_free_space)
        print(f"  LCKLSTS DAT  {self.cur_base:02X}00H  {lock_pages:02X}00H")

        # Console data region
        if cfg.num_consoles > 0:
            prev_base = self.cur_base
            self.cur_base -= cfg.num_consoles
            self.console_dat_base = self.cur_base
            self.system_data[244] = self.console_dat_base
            print(f"  CONSOLE DAT  {self.cur_base:02X}00H  {cfg.num_consoles:02X}00H")

        # Set up memory segment table
        self.setup_memory_segments()

        # NOW create memory image covering from cur_base to just below SYSTEM.DAT
        # This is AFTER all modules have been loaded and addresses calculated
        # Note: SYSTEM.DAT occupies mem_top*256 to mem_top*256+255 (record 0-1)
        # The code image covers cur_base*256 to mem_top*256-1 (records 2+)
        self.image_base = self.cur_base * 256
        image_size = cfg.mem_top * 256 - self.image_base  # Don't include SYSTEM.DAT area
        self.mem_image = bytearray(image_size)
        print(f"\n  Memory image: {self.image_base:04X}H - {cfg.mem_top * 256 - 1:04X}H ({image_size} bytes)")

        # Write all modules to memory image
        for module in modules_to_write:
            self.write_module(module)

        # Copy XIOS jump table to XIOSJMP TBL location
        # The XIOSJMP TBL is at xios_jmp_tbl_base (FB00H typically)
        # This provides a fixed entry point for XIOS calls from any bank
        xios_tbl_offset = self.xios_jmp_tbl_base * 256 - self.image_base
        if xios_tbl_offset >= 0 and xios_tbl_offset + 256 <= len(self.mem_image):
            self.mem_image[xios_tbl_offset:xios_tbl_offset + 256] = self.bnkxios000
            print(f"  Copied XIOS jump table to {self.xios_jmp_tbl_base:02X}00H")

        # Calculate record count
        self.record_count = (256 + len(self.mem_image) + 127) // 128

        # Finalize system data
        struct.pack_into('<H', self.system_data, 120, self.record_count)  # nmb_records
        # Note: DRI GENSYS does NOT set bytes 252-253 (sysdatadr in SYSTEM.DAT).
        # MPMLDR calculates this from mem_top at runtime.

        # Update RSP links
        self.system_data[125] = len(self.rsps)  # nmb_rsps
        if self.rsps:
            struct.pack_into('<H', self.system_data, 254, rsp_link)  # rspl

        # Build final output: SYSTEM.DAT + memory image
        # Pad memory image to 128-byte boundary
        padded_image = self.mem_image + bytes((-len(self.mem_image)) % 128)

        # IMPORTANT: MPMLDR loads records DOWNWARD from mem_top!
        # Record 2 gets loaded to (mem_top*256 - 128), record 3 to (mem_top*256 - 256), etc.
        # So the file must contain high addresses first, low addresses last.
        # We need to reverse the 128-byte records in the image.
        num_records = len(padded_image) // 128
        reversed_image = bytearray()
        for i in range(num_records - 1, -1, -1):
            reversed_image.extend(padded_image[i * 128:(i + 1) * 128])

        self.output = self.system_data + reversed_image
        self.record_count = len(self.output) // 128

        print(f"\nGenerated {self.record_count} records ({len(self.output)} bytes)")

    def setup_memory_segments(self) -> None:
        """Set up memory segment table in system data."""
        cfg = self.config

        print("\nMemory Segments:")

        # First segment is always the MP/M II system (pre-allocated)
        seg_offset = 16
        self.system_data[seg_offset] = self.cur_base
        self.system_data[seg_offset + 1] = 0xFF - self.cur_base + 1
        self.system_data[seg_offset + 2] = 0x80  # Pre-allocated
        self.system_data[seg_offset + 3] = 0  # Bank 0
        print(f"  MP/M II Sys  {self.cur_base:02X}00H  {0xFF - self.cur_base + 1:02X}00H  Bank 0")

        # User segments (one per bank for bank-switched systems)
        total_segments = 1  # Count the system segment
        for i in range(1, cfg.num_mem_segments + 1):
            seg_offset = 16 + i * 4
            if seg_offset >= 48:
                break
            # User segment from 0 to common base
            self.system_data[seg_offset] = 0
            self.system_data[seg_offset + 1] = cfg.common_base
            self.system_data[seg_offset + 2] = 0  # Not pre-allocated
            self.system_data[seg_offset + 3] = i  # Bank number
            print(f"  Memseg  Usr  0000H  {cfg.common_base:02X}00H  Bank {i}")
            total_segments += 1

        # Update nmb_mem_seg to actual count (MPMLDR loops 0 to nmb_mem_seg-1)
        self.system_data[15] = total_segments

    def write_output(self, path: Path) -> None:
        """Write MPM.SYS to file."""
        path.write_bytes(self.output)
        print(f"\nWrote {len(self.output)} bytes to {path}")

    def write_system_dat(self, path: Path) -> None:
        """Write SYSTEM.DAT to separate file."""
        path.write_bytes(self.system_data)
        print(f"Wrote {len(self.system_data)} bytes to {path}")


def create_default_config(path: Path) -> None:
    """Create a default configuration file."""
    config = SystemConfig()
    config.rsps = [
        {"name": "SFTP", "rsp": "sftp.rsp", "brs": "sftp.brs"},
    ]
    config.to_json(path)
    print(f"Created default configuration: {path}")


def main():
    parser = argparse.ArgumentParser(
        description='MP/M II System Generator',
        epilog='Creates MPM.SYS from SPR modules with proper PRL relocation.'
    )
    parser.add_argument('config', nargs='?', help='JSON configuration file')
    parser.add_argument('-o', '--output', help='Output MPM.SYS file')
    parser.add_argument('--init', metavar='FILE', help='Create default config file')
    parser.add_argument('-v', '--verbose', action='store_true', help='Verbose output')

    args = parser.parse_args()

    if args.init:
        create_default_config(Path(args.init))
        return 0

    if not args.config:
        parser.print_help()
        return 1

    try:
        config = SystemConfig.from_json(Path(args.config))

        if args.output:
            config.output = args.output

        generator = SystemGenerator(config)
        generator.generate()

        output_path = Path(config.spr_dir) / config.output
        generator.write_output(output_path)

        sysdat_path = Path(config.spr_dir) / config.system_dat
        generator.write_system_dat(sysdat_path)

        return 0

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        if args.verbose:
            import traceback
            traceback.print_exc()
        return 1


if __name__ == '__main__':
    sys.exit(main())
