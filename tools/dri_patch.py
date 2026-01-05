#!/usr/bin/env python3
"""
dri_patch - Binary patching tool for DRI-style builds

Combines Intel HEX and binary files at specified addresses, similar to
DRI's "pip file.hex=a.hex[I],b.hex[I],c.hex[H]" followed by "load file".

Usage examples:
  # Create new binary from Intel HEX files
  dri_patch -o output.com --size 0x1800 \
      --hex mpmldr.hex \
      --hex ldrbdos.hex \
      --hex ldrbios.hex

  # Patch binary at specific address
  dri_patch -o output.bin --base input.bin \
      --patch 0x0D00 ldrbdos.bin

  # Copy bytes from one file to another at specific offsets
  # Format: SRC:SRCOFF:LEN@DSTOFF
  dri_patch -o mpmldr.com --base mpmldr.com \
      --copy resbdos.spr:509:2@133

  # Extract region from binary
  dri_patch --extract 0x100:0x200 input.bin -o region.bin

  # Create padded binary
  dri_patch -o output.bin --size 0x10000 --hex small.hex

Copyright (C) 2024
SPDX-License-Identifier: GPL-3.0-or-later
"""

import argparse
import sys
import os
from pathlib import Path


def parse_hex_address(s: str) -> int:
    """Parse hex or decimal address string."""
    s = s.strip()
    if s.startswith('0x') or s.startswith('0X'):
        return int(s, 16)
    elif s.endswith('h') or s.endswith('H'):
        return int(s[:-1], 16)
    elif all(c in '0123456789abcdefABCDEF' for c in s) and any(c in 'abcdefABCDEF' for c in s):
        # Looks like hex without prefix
        return int(s, 16)
    else:
        return int(s, 0)  # Auto-detect base


def parse_intel_hex(data: bytes) -> tuple[dict[int, int], int, int]:
    """
    Parse Intel HEX format.
    Returns (address_to_byte_dict, min_addr, max_addr)
    """
    result = {}
    min_addr = None
    max_addr = None
    extended_addr = 0

    lines = data.decode('ascii', errors='ignore').split('\n')

    for line_num, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue
        if not line.startswith(':'):
            continue

        try:
            # Parse Intel HEX record
            hex_data = bytes.fromhex(line[1:])
            byte_count = hex_data[0]
            address = (hex_data[1] << 8) | hex_data[2]
            record_type = hex_data[3]

            # Verify checksum
            checksum = sum(hex_data) & 0xFF
            if checksum != 0:
                raise ValueError(f"Line {line_num}: Bad checksum")

            if record_type == 0x00:  # Data record
                full_addr = extended_addr + address
                for i, byte in enumerate(hex_data[4:4+byte_count]):
                    addr = full_addr + i
                    result[addr] = byte
                    if min_addr is None or addr < min_addr:
                        min_addr = addr
                    if max_addr is None or addr > max_addr:
                        max_addr = addr

            elif record_type == 0x01:  # EOF
                break

            elif record_type == 0x02:  # Extended segment address
                extended_addr = ((hex_data[4] << 8) | hex_data[5]) << 4

            elif record_type == 0x04:  # Extended linear address
                extended_addr = ((hex_data[4] << 8) | hex_data[5]) << 16

        except Exception as e:
            raise ValueError(f"Line {line_num}: {e}")

    if not result:
        raise ValueError("No data records found in Intel HEX file")

    return result, min_addr or 0, max_addr or 0


def load_intel_hex(filepath: Path) -> tuple[dict[int, int], int, int]:
    """Load Intel HEX file and return (addr_to_byte, min_addr, max_addr)."""
    with open(filepath, 'rb') as f:
        return parse_intel_hex(f.read())


def load_binary(filepath: Path, offset: int = 0) -> tuple[dict[int, int], int, int]:
    """Load binary file at given offset."""
    with open(filepath, 'rb') as f:
        data = f.read()

    result = {}
    for i, byte in enumerate(data):
        result[offset + i] = byte

    min_addr = offset
    max_addr = offset + len(data) - 1 if data else offset
    return result, min_addr, max_addr


def dict_to_binary(addr_dict: dict[int, int], start: int, size: int, fill: int = 0) -> bytes:
    """Convert address dictionary to binary, filling gaps."""
    result = bytearray([fill] * size)
    for addr, byte in addr_dict.items():
        idx = addr - start
        if 0 <= idx < size:
            result[idx] = byte
    return bytes(result)


def apply_patch(base: dict[int, int], patch: dict[int, int],
                overflow_error: bool = True, verbose: bool = False,
                max_addr: int = None) -> dict[int, int]:
    """Apply patch to base, optionally checking for overflow."""
    result = base.copy()

    for addr, byte in patch.items():
        if max_addr is not None and addr > max_addr:
            msg = f"Patch address 0x{addr:04X} exceeds max 0x{max_addr:04X}"
            if overflow_error:
                raise ValueError(msg)
            else:
                print(f"Warning: {msg}", file=sys.stderr)
                continue
        result[addr] = byte

    return result


def extract_region(data: bytes, start: int, end: int) -> bytes:
    """Extract region from binary data."""
    return data[start:end]


def write_intel_hex(addr_dict: dict[int, int], filepath: Path,
                    bytes_per_line: int = 16):
    """Write address dictionary as Intel HEX file."""
    if not addr_dict:
        with open(filepath, 'w') as f:
            f.write(":00000001FF\n")  # EOF record
        return

    min_addr = min(addr_dict.keys())
    max_addr = max(addr_dict.keys())

    with open(filepath, 'w') as f:
        addr = min_addr
        while addr <= max_addr:
            # Collect bytes for this line
            line_bytes = []
            line_start = addr
            while addr <= max_addr and len(line_bytes) < bytes_per_line:
                if addr in addr_dict:
                    line_bytes.append(addr_dict[addr])
                else:
                    # Gap - end this line
                    if line_bytes:
                        break
                    # Skip gap
                    addr += 1
                    line_start = addr
                    continue
                addr += 1

            if line_bytes:
                # Write data record
                byte_count = len(line_bytes)
                addr_hi = (line_start >> 8) & 0xFF
                addr_lo = line_start & 0xFF
                record = [byte_count, addr_hi, addr_lo, 0x00] + line_bytes
                checksum = (-sum(record)) & 0xFF
                hex_str = ''.join(f'{b:02X}' for b in record) + f'{checksum:02X}'
                f.write(f":{hex_str}\n")

        # EOF record
        f.write(":00000001FF\n")


def main():
    parser = argparse.ArgumentParser(
        description='Binary patching tool for DRI-style builds',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Combine Intel HEX files into COM file
  %(prog)s -o mpmldr.com --size 0x1680 \\
      --hex impmldr.hex --hex ldrbdos.hex --hex ldrbios.hex

  # Start with base binary and patch at address
  %(prog)s -o output.bin --base input.bin \\
      --patch 0x0D00:ldrbdos.bin

  # Copy bytes from one file to another (e.g., patch serial number)
  %(prog)s -o mpmldr.com --base mpmldr.com \\
      --copy resbdos.spr:509:2@133

  # Extract region from binary
  %(prog)s --extract 0x100:0x300 input.bin -o region.bin

  # Show info about Intel HEX file
  %(prog)s --info file.hex
        """
    )

    parser.add_argument('-o', '--output', type=Path,
                        help='Output file path')
    parser.add_argument('--base', type=str,
                        help='Base file to start with (binary or hex). '
                             'Format: FILE or FILE@OFFSET')
    parser.add_argument('--hex', action='append', default=[], metavar='FILE',
                        help='Add Intel HEX file (uses addresses from file)')
    parser.add_argument('--patch', action='append', default=[], metavar='ADDR:FILE',
                        help='Patch binary file at address (e.g., 0x0D00:file.bin)')
    parser.add_argument('--copy', action='append', default=[], metavar='SRC:OFF:LEN@DST',
                        help='Copy bytes from source file at offset to dest offset '
                             '(e.g., src.bin:509:2@133)')
    parser.add_argument('--size', type=str,
                        help='Output size (pads with zeros or truncates)')
    parser.add_argument('--start', type=str, default='0',
                        help='Starting address for output (default: lowest address or 0)')
    parser.add_argument('--fill', type=str, default='0',
                        help='Fill byte for gaps (default: 0)')
    parser.add_argument('--extract', type=str, metavar='START:END',
                        help='Extract region from input file')
    parser.add_argument('--info', action='store_true',
                        help='Show info about input files')
    parser.add_argument('--overflow', choices=['error', 'warn', 'ignore'],
                        default='error',
                        help='Behavior on overflow (default: error)')
    parser.add_argument('--hex-output', action='store_true',
                        help='Output as Intel HEX instead of binary')
    parser.add_argument('-v', '--verbose', action='store_true',
                        help='Verbose output')
    parser.add_argument('input', nargs='*', type=Path,
                        help='Input files (for --extract or --info)')

    args = parser.parse_args()

    # Handle --info mode
    if args.info:
        for filepath in args.input + [Path(h) for h in args.hex]:
            if not filepath.exists():
                print(f"Error: {filepath} not found", file=sys.stderr)
                continue

            print(f"\n=== {filepath} ===")
            print(f"Size: {filepath.stat().st_size} bytes")

            # Try to parse as Intel HEX
            try:
                addr_dict, min_addr, max_addr = load_intel_hex(filepath)
                print(f"Format: Intel HEX")
                print(f"Address range: 0x{min_addr:04X} - 0x{max_addr:04X}")
                print(f"Data bytes: {len(addr_dict)}")
                print(f"Span: {max_addr - min_addr + 1} bytes")
            except:
                # Binary file
                with open(filepath, 'rb') as f:
                    data = f.read()
                print(f"Format: Binary")
                print(f"First bytes: {data[:16].hex()}")
        return 0

    # Handle --extract mode
    if args.extract:
        if not args.input:
            print("Error: Input file required for --extract", file=sys.stderr)
            return 1
        if not args.output:
            print("Error: Output file required for --extract", file=sys.stderr)
            return 1

        try:
            start_str, end_str = args.extract.split(':')
            start = parse_hex_address(start_str)
            end = parse_hex_address(end_str)
        except ValueError:
            print(f"Error: Invalid extract range '{args.extract}'", file=sys.stderr)
            return 1

        with open(args.input[0], 'rb') as f:
            data = f.read()

        extracted = data[start:end]
        if args.verbose:
            print(f"Extracting 0x{start:04X}:0x{end:04X} ({len(extracted)} bytes)")

        with open(args.output, 'wb') as f:
            f.write(extracted)

        print(f"Wrote {len(extracted)} bytes to {args.output}")
        return 0

    # Build mode - combine files
    if not args.output and not args.info:
        print("Error: Output file required (-o)", file=sys.stderr)
        return 1

    # Start with empty or base
    combined = {}
    global_min = None
    global_max = None

    def update_bounds(min_a, max_a):
        nonlocal global_min, global_max
        if global_min is None or min_a < global_min:
            global_min = min_a
        if global_max is None or max_a > global_max:
            global_max = max_a

    # Load base file if specified
    if args.base:
        if '@' in args.base:
            base_file, offset_str = args.base.rsplit('@', 1)
            offset = parse_hex_address(offset_str)
        else:
            base_file = args.base
            offset = 0

        base_path = Path(base_file)
        if not base_path.exists():
            print(f"Error: Base file not found: {base_file}", file=sys.stderr)
            return 1

        # Try Intel HEX first, fall back to binary
        try:
            combined, min_a, max_a = load_intel_hex(base_path)
            if args.verbose:
                print(f"Loaded base HEX: {base_file} (0x{min_a:04X}-0x{max_a:04X})")
        except:
            combined, min_a, max_a = load_binary(base_path, offset)
            if args.verbose:
                print(f"Loaded base binary: {base_file} at 0x{offset:04X} ({max_a - min_a + 1} bytes)")

        update_bounds(min_a, max_a)

    # Add Intel HEX files
    for hex_file in args.hex:
        hex_path = Path(hex_file)
        if not hex_path.exists():
            print(f"Error: HEX file not found: {hex_file}", file=sys.stderr)
            return 1

        addr_dict, min_a, max_a = load_intel_hex(hex_path)
        if args.verbose:
            print(f"Adding HEX: {hex_file} (0x{min_a:04X}-0x{max_a:04X}, {len(addr_dict)} bytes)")

        combined = apply_patch(combined, addr_dict,
                               overflow_error=(args.overflow == 'error'),
                               verbose=args.verbose)
        update_bounds(min_a, max_a)

    # Add binary patches
    for patch_spec in args.patch:
        try:
            addr_str, file_path = patch_spec.split(':', 1)
            addr = parse_hex_address(addr_str)
        except ValueError:
            print(f"Error: Invalid patch spec '{patch_spec}' (use ADDR:FILE)", file=sys.stderr)
            return 1

        patch_path = Path(file_path)
        if not patch_path.exists():
            print(f"Error: Patch file not found: {file_path}", file=sys.stderr)
            return 1

        addr_dict, min_a, max_a = load_binary(patch_path, addr)
        if args.verbose:
            print(f"Adding patch: {file_path} at 0x{addr:04X} ({max_a - min_a + 1} bytes)")

        combined = apply_patch(combined, addr_dict,
                               overflow_error=(args.overflow == 'error'),
                               verbose=args.verbose)
        update_bounds(min_a, max_a)

    # Process --copy operations (copy bytes from source file to dest offset)
    for copy_spec in args.copy:
        try:
            # Parse SRC:SRCOFF:LEN@DSTOFF format
            if '@' not in copy_spec:
                raise ValueError("Missing @DSTOFF")
            src_part, dst_off_str = copy_spec.rsplit('@', 1)
            parts = src_part.split(':')
            if len(parts) != 3:
                raise ValueError("Expected SRC:SRCOFF:LEN format")
            src_file, src_off_str, length_str = parts
            src_off = parse_hex_address(src_off_str)
            length = parse_hex_address(length_str)
            dst_off = parse_hex_address(dst_off_str)
        except ValueError as e:
            print(f"Error: Invalid copy spec '{copy_spec}' (use SRC:SRCOFF:LEN@DSTOFF): {e}",
                  file=sys.stderr)
            return 1

        src_path = Path(src_file)
        if not src_path.exists():
            print(f"Error: Source file not found: {src_file}", file=sys.stderr)
            return 1

        # Read bytes from source file
        with open(src_path, 'rb') as f:
            f.seek(src_off)
            src_bytes = f.read(length)

        if len(src_bytes) < length:
            print(f"Warning: Only read {len(src_bytes)} bytes from {src_file} "
                  f"(requested {length})", file=sys.stderr)

        # Create address dict for the copied bytes
        addr_dict = {}
        for i, byte in enumerate(src_bytes):
            addr_dict[dst_off + i] = byte

        if args.verbose:
            bytes_hex = src_bytes.hex()
            print(f"Copying {len(src_bytes)} bytes from {src_file}@0x{src_off:X} "
                  f"to 0x{dst_off:X}: {bytes_hex}")

        combined = apply_patch(combined, addr_dict,
                               overflow_error=(args.overflow == 'error'),
                               verbose=args.verbose)
        update_bounds(dst_off, dst_off + len(src_bytes) - 1)

    # Also process input files as additional hex files (for convenience)
    for input_file in args.input:
        if not input_file.exists():
            print(f"Error: Input file not found: {input_file}", file=sys.stderr)
            return 1

        try:
            addr_dict, min_a, max_a = load_intel_hex(input_file)
            if args.verbose:
                print(f"Adding HEX: {input_file} (0x{min_a:04X}-0x{max_a:04X})")
            combined = apply_patch(combined, addr_dict,
                                   overflow_error=(args.overflow == 'error'),
                                   verbose=args.verbose)
            update_bounds(min_a, max_a)
        except:
            print(f"Error: Could not parse {input_file} as Intel HEX", file=sys.stderr)
            return 1

    if not combined:
        print("Error: No data to write", file=sys.stderr)
        return 1

    # Determine output parameters
    start_addr = parse_hex_address(args.start) if args.start != '0' else (global_min or 0)
    fill_byte = parse_hex_address(args.fill)

    if args.size:
        output_size = parse_hex_address(args.size)
    else:
        output_size = (global_max or 0) - start_addr + 1

    # Check for data outside output range
    for addr in combined.keys():
        if addr < start_addr or addr >= start_addr + output_size:
            msg = f"Data at 0x{addr:04X} outside output range 0x{start_addr:04X}-0x{start_addr + output_size - 1:04X}"
            if args.overflow == 'error':
                print(f"Error: {msg}", file=sys.stderr)
                return 1
            elif args.overflow == 'warn':
                print(f"Warning: {msg}", file=sys.stderr)

    if args.verbose:
        print(f"Output: 0x{start_addr:04X}-0x{start_addr + output_size - 1:04X} ({output_size} bytes)")

    # Write output
    if args.hex_output:
        write_intel_hex(combined, args.output)
        print(f"Wrote Intel HEX to {args.output}")
    else:
        output_data = dict_to_binary(combined, start_addr, output_size, fill_byte)
        with open(args.output, 'wb') as f:
            f.write(output_data)
        print(f"Wrote {len(output_data)} bytes to {args.output}")

    return 0


if __name__ == '__main__':
    sys.exit(main())
