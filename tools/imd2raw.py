#!/usr/bin/env python3
"""Convert IMD (ImageDisk) format to raw disk image."""

import sys
import struct

def parse_imd(filename):
    """Parse IMD file and return track data."""
    with open(filename, 'rb') as f:
        data = f.read()

    # Find end of header (0x1A)
    header_end = data.find(b'\x1a')
    if header_end < 0:
        raise ValueError("Invalid IMD file: no header terminator")

    header = data[:header_end].decode('ascii', errors='replace')
    print(f"Header: {header[:100]}...")

    pos = header_end + 1
    tracks = []

    sector_sizes = [128, 256, 512, 1024, 2048, 4096, 8192]

    while pos < len(data):
        if pos + 5 > len(data):
            break

        mode = data[pos]
        cyl = data[pos + 1]
        head = data[pos + 2]
        num_sectors = data[pos + 3]
        size_code = data[pos + 4]
        pos += 5

        has_cyl_map = (head & 0x80) != 0
        has_head_map = (head & 0x40) != 0
        head = head & 0x3F

        if size_code >= len(sector_sizes):
            print(f"Warning: Invalid sector size code {size_code}")
            break

        sector_size = sector_sizes[size_code]

        # Read sector number map
        if pos + num_sectors > len(data):
            break
        sector_map = list(data[pos:pos + num_sectors])
        pos += num_sectors

        # Optional cylinder map
        cyl_map = None
        if has_cyl_map:
            cyl_map = list(data[pos:pos + num_sectors])
            pos += num_sectors

        # Optional head map
        head_map = None
        if has_head_map:
            head_map = list(data[pos:pos + num_sectors])
            pos += num_sectors

        # Read sector data
        sectors = {}
        for i in range(num_sectors):
            if pos >= len(data):
                break

            stype = data[pos]
            pos += 1

            if stype == 0:
                # Unavailable
                sector_data = b'\xe5' * sector_size
            elif stype == 1:
                # Normal data
                sector_data = data[pos:pos + sector_size]
                pos += sector_size
            elif stype == 2:
                # Compressed (all same byte)
                fill = data[pos]
                pos += 1
                sector_data = bytes([fill]) * sector_size
            else:
                # Other types (deleted, errors) - treat as normal
                if stype in [3, 4, 5, 6, 7, 8]:
                    if stype in [1, 3, 5, 7]:
                        sector_data = data[pos:pos + sector_size]
                        pos += sector_size
                    else:
                        fill = data[pos]
                        pos += 1
                        sector_data = bytes([fill]) * sector_size
                else:
                    sector_data = b'\xe5' * sector_size

            sectors[sector_map[i]] = sector_data

        tracks.append({
            'cyl': cyl,
            'head': head,
            'sectors': sectors,
            'sector_size': sector_size,
            'num_sectors': num_sectors
        })

    return tracks

def imd_to_raw(imd_file, raw_file):
    """Convert IMD to raw disk image."""
    tracks = parse_imd(imd_file)

    if not tracks:
        print("No tracks found!")
        return

    # Determine geometry
    max_cyl = max(t['cyl'] for t in tracks)
    max_head = max(t['head'] for t in tracks)
    sector_size = tracks[0]['sector_size']
    sectors_per_track = tracks[0]['num_sectors']

    print(f"Geometry: {max_cyl + 1} cyls, {max_head + 1} heads, {sectors_per_track} sectors, {sector_size} bytes/sector")

    # Create raw image
    total_size = (max_cyl + 1) * (max_head + 1) * sectors_per_track * sector_size
    image = bytearray(b'\xe5' * total_size)

    for track in tracks:
        cyl = track['cyl']
        head = track['head']

        for sec_num, sec_data in track['sectors'].items():
            # Calculate offset (sectors typically numbered 1-N)
            sec_idx = sec_num - 1 if sec_num > 0 else sec_num
            offset = ((cyl * (max_head + 1) + head) * sectors_per_track + sec_idx) * sector_size

            if offset + len(sec_data) <= len(image):
                image[offset:offset + len(sec_data)] = sec_data

    with open(raw_file, 'wb') as f:
        f.write(image)

    print(f"Wrote {len(image)} bytes to {raw_file}")
    return sector_size, sectors_per_track, max_cyl + 1

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} input.imd output.img")
        sys.exit(1)

    imd_to_raw(sys.argv[1], sys.argv[2])
