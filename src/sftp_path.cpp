// sftp_path.cpp - SFTP path parsing and CP/M directory access
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sftp_path.h"
#include "disk.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iostream>
#include <set>
#include <map>

SftpPath parse_sftp_path(const std::string& path) {
    SftpPath result;
    result.drive = -1;
    result.user = -1;
    result.filename = "";

    if (path.empty() || path == "/" || path == ".") {
        return result;  // Root
    }

    std::string p = path;

    // Remove leading slash
    if (p[0] == '/') {
        p = p.substr(1);
    }

    // Remove trailing slash
    while (!p.empty() && p.back() == '/') {
        p.pop_back();
    }

    if (p.empty()) {
        return result;  // Root
    }

    // First component should be drive letter or drive.user
    size_t slash = p.find('/');
    std::string drive_part = (slash != std::string::npos) ? p.substr(0, slash) : p;
    std::string rest = (slash != std::string::npos) ? p.substr(slash + 1) : "";

    // Parse drive letter (A-P)
    if (drive_part.empty()) {
        return result;
    }

    char drive_letter = std::toupper(static_cast<unsigned char>(drive_part[0]));
    if (drive_letter < 'A' || drive_letter > 'P') {
        return result;  // Invalid drive
    }
    result.drive = drive_letter - 'A';

    // Check for .N user suffix (e.g., "A.5" or "A.15")
    size_t dot = drive_part.find('.');
    if (dot != std::string::npos && dot == 1) {
        // User number after dot
        std::string user_str = drive_part.substr(2);
        if (!user_str.empty()) {
            int user = std::stoi(user_str);
            if (user >= 0 && user <= 15) {
                result.user = user;
            }
        }
    } else if (drive_part.length() == 1) {
        // Just drive letter, default to user 0
        result.user = 0;
    }

    // Rest is filename
    if (!rest.empty()) {
        result.filename = rest;
        // Uppercase for CP/M
        std::transform(result.filename.begin(), result.filename.end(),
                       result.filename.begin(),
                       [](unsigned char c) { return std::toupper(c); });
    }

    return result;
}

std::string sftp_path_to_string(const SftpPath& path) {
    if (path.drive < 0) {
        return "/";
    }

    std::string result = "/";
    result += static_cast<char>('A' + path.drive);

    if (path.user >= 0) {
        result += ".";
        result += std::to_string(path.user);
    }

    if (!path.filename.empty()) {
        result += "/";
        result += path.filename;
    }

    return result;
}

std::string CpmDirEntry::get_filename() const {
    std::string name;

    // Copy filename, stripping spaces and high bits
    for (int i = 0; i < 8; i++) {
        char c = filename[i] & 0x7F;
        if (c != ' ') {
            name += c;
        }
    }

    // Add extension if present
    bool has_ext = false;
    for (int i = 0; i < 3; i++) {
        char c = extension[i] & 0x7F;
        if (c != ' ') {
            if (!has_ext) {
                name += '.';
                has_ext = true;
            }
            name += c;
        }
    }

    return name;
}

uint32_t CpmDirEntry::get_size(uint16_t block_size) const {
    // Each extent holds up to 16KB (128 records * 128 bytes)
    // record_count is records in this extent (0-128)
    // Extent number contributes 16KB per extent
    uint32_t extent_records = static_cast<uint32_t>(extent) * 128;
    return (extent_records + record_count) * 128;
}

std::vector<int> get_mounted_drives() {
    std::vector<int> drives;
    DiskSystem& ds = DiskSystem::instance();

    for (int i = 0; i < DiskSystem::MAX_DISKS; i++) {
        if (ds.get(i) && ds.get(i)->is_open()) {
            drives.push_back(i);
        }
    }

    return drives;
}

std::vector<SftpDirEntry> read_directory(int drive, int user) {
    std::vector<SftpDirEntry> entries;

    DiskSystem& ds = DiskSystem::instance();
    Disk* disk = ds.get(drive);
    if (!disk || !disk->is_open()) {
        return entries;
    }

    const DiskParameterBlock& dpb = disk->dpb();

    // Calculate directory location
    // Directory starts at track 'off' (system tracks)
    // Directory entries are 32 bytes each
    // drm+1 = number of directory entries
    uint16_t dir_entries = dpb.drm + 1;
    uint16_t sector_size = disk->sector_size();
    uint16_t entries_per_sector = sector_size / 32;
    uint16_t dir_sectors = (dir_entries + entries_per_sector - 1) / entries_per_sector;

    // Map to aggregate file info across extents
    // Key: "user:filename"
    std::map<std::string, SftpDirEntry> file_map;

    std::vector<uint8_t> sector_buf(sector_size);

    // Read directory sectors
    uint16_t track = dpb.off;  // First directory track
    uint16_t sector = 1;       // Sectors are 1-based in CP/M

    for (uint16_t s = 0; s < dir_sectors; s++) {
        disk->set_track(track);
        disk->set_sector(sector);

        if (disk->read_sector(sector_buf.data()) != 0) {
            break;  // Read error
        }

        // Process entries in this sector
        for (uint16_t e = 0; e < entries_per_sector; e++) {
            CpmDirEntry* entry = reinterpret_cast<CpmDirEntry*>(
                &sector_buf[e * 32]);

            if (entry->is_deleted()) {
                continue;
            }

            if (entry->user > 15) {
                continue;  // Invalid user number
            }

            // Filter by user if specified
            if (user >= 0 && entry->user != user) {
                continue;
            }

            std::string filename = entry->get_filename();

            // Skip entries with invalid characters (control chars, non-printable)
            bool valid = !filename.empty();
            for (char c : filename) {
                if (c < 0x20 || c > 0x7E) {
                    valid = false;
                    break;
                }
            }
            if (!valid) {
                continue;
            }

            std::string key = std::to_string(entry->user) + ":" + filename;

            auto it = file_map.find(key);
            if (it == file_map.end()) {
                // New file
                SftpDirEntry sentry;
                sentry.name = filename;
                sentry.user = entry->user;
                sentry.size = entry->get_size(1 << (dpb.bsh + 7));
                sentry.is_directory = false;
                sentry.is_system = entry->is_system();
                sentry.is_read_only = entry->is_read_only();
                file_map[key] = sentry;
            } else {
                // Update size from additional extent
                uint32_t extent_size = entry->get_size(1 << (dpb.bsh + 7));
                if (extent_size > it->second.size) {
                    it->second.size = extent_size;
                }
                // Merge attributes
                if (entry->is_system()) it->second.is_system = true;
                if (entry->is_read_only()) it->second.is_read_only = true;
            }
        }

        // Move to next sector
        sector++;
        if (sector > dpb.spt) {
            sector = 1;
            track++;
        }
    }

    // Convert map to vector
    for (auto& pair : file_map) {
        entries.push_back(std::move(pair.second));
    }

    // Sort by name
    std::sort(entries.begin(), entries.end(),
              [](const SftpDirEntry& a, const SftpDirEntry& b) {
                  return a.name < b.name;
              });

    return entries;
}

std::vector<int> get_active_users(int drive) {
    std::set<int> users;

    DiskSystem& ds = DiskSystem::instance();
    Disk* disk = ds.get(drive);
    if (!disk || !disk->is_open()) {
        return {};
    }

    const DiskParameterBlock& dpb = disk->dpb();
    uint16_t dir_entries = dpb.drm + 1;
    uint16_t sector_size = disk->sector_size();
    uint16_t entries_per_sector = sector_size / 32;
    uint16_t dir_sectors = (dir_entries + entries_per_sector - 1) / entries_per_sector;

    std::vector<uint8_t> sector_buf(sector_size);

    uint16_t track = dpb.off;
    uint16_t sector = 1;

    for (uint16_t s = 0; s < dir_sectors; s++) {
        disk->set_track(track);
        disk->set_sector(sector);

        if (disk->read_sector(sector_buf.data()) != 0) {
            break;
        }

        for (uint16_t e = 0; e < entries_per_sector; e++) {
            CpmDirEntry* entry = reinterpret_cast<CpmDirEntry*>(
                &sector_buf[e * 32]);

            if (!entry->is_deleted() && entry->user <= 15) {
                users.insert(entry->user);
            }
        }

        sector++;
        if (sector > dpb.spt) {
            sector = 1;
            track++;
        }
    }

    return std::vector<int>(users.begin(), users.end());
}

// Helper to match CP/M filename (case-insensitive, handles 8.3)
static bool filename_matches(const CpmDirEntry* entry, const std::string& name) {
    std::string entry_name = entry->get_filename();
    if (entry_name.size() != name.size()) return false;
    for (size_t i = 0; i < name.size(); i++) {
        if (std::toupper(static_cast<unsigned char>(entry_name[i])) !=
            std::toupper(static_cast<unsigned char>(name[i]))) {
            return false;
        }
    }
    return true;
}

std::optional<CpmFileInfo> find_file(int drive, int user, const std::string& filename) {
    DiskSystem& ds = DiskSystem::instance();
    Disk* disk = ds.get(drive);
    if (!disk || !disk->is_open()) {
        return std::nullopt;
    }

    const DiskParameterBlock& dpb = disk->dpb();
    uint16_t dir_entries = dpb.drm + 1;
    uint16_t sector_size = disk->sector_size();
    uint16_t entries_per_sector = sector_size / 32;
    uint16_t dir_sectors = (dir_entries + entries_per_sector - 1) / entries_per_sector;

    // Block size and allocation entries
    uint16_t block_size = 1 << (dpb.bsh + 7);  // 128 * 2^bsh
    bool big_disk = (dpb.dsm > 255);  // 16-bit block numbers if dsm > 255
    int alloc_entries = big_disk ? 8 : 16;

    CpmFileInfo file;
    file.drive = drive;
    file.user = user;
    file.filename = filename;
    file.size = 0;
    file.is_read_only = false;

    std::vector<uint8_t> sector_buf(sector_size);
    uint16_t track = dpb.off;
    uint16_t sector = 1;

    // Scan directory for matching entries
    for (uint16_t s = 0; s < dir_sectors; s++) {
        disk->set_track(track);
        disk->set_sector(sector);

        if (disk->read_sector(sector_buf.data()) != 0) {
            break;
        }

        for (uint16_t e = 0; e < entries_per_sector; e++) {
            CpmDirEntry* entry = reinterpret_cast<CpmDirEntry*>(
                &sector_buf[e * 32]);

            if (entry->is_deleted() || entry->user != user) {
                continue;
            }

            if (!filename_matches(entry, filename)) {
                continue;
            }

            // Found matching extent
            CpmFileInfo::Extent ext;
            ext.extent_num = entry->extent + (entry->s2 * 32);
            ext.record_count = entry->record_count;

            // Extract allocation blocks
            for (int i = 0; i < alloc_entries; i++) {
                uint16_t block;
                if (big_disk) {
                    block = entry->allocation[i * 2] |
                            (entry->allocation[i * 2 + 1] << 8);
                } else {
                    block = entry->allocation[i];
                }
                if (block != 0) {
                    ext.blocks.push_back(block);
                }
            }

            file.extents.push_back(ext);
            if (entry->is_read_only()) {
                file.is_read_only = true;
            }
        }

        sector++;
        if (sector > dpb.spt) {
            sector = 1;
            track++;
        }
    }

    if (file.extents.empty()) {
        return std::nullopt;
    }

    // Sort extents by extent number
    std::sort(file.extents.begin(), file.extents.end(),
              [](const CpmFileInfo::Extent& a, const CpmFileInfo::Extent& b) {
                  return a.extent_num < b.extent_num;
              });

    // Calculate total size
    // Each extent can hold 128 records * 128 bytes = 16KB
    // The last extent's record_count gives the actual records
    uint32_t total_records = 0;
    for (size_t i = 0; i < file.extents.size(); i++) {
        if (i < file.extents.size() - 1) {
            // Not the last extent - assume full 128 records
            total_records += 128;
        } else {
            // Last extent - use record_count
            total_records += file.extents[i].record_count;
        }
    }
    file.size = total_records * 128;

    return file;
}

std::vector<uint8_t> read_file_data(const CpmFileInfo& file, uint64_t offset, uint32_t length) {
    std::vector<uint8_t> data;

    if (offset >= file.size) {
        return data;  // Beyond EOF
    }

    // Clamp length to available data
    if (offset + length > file.size) {
        length = file.size - offset;
    }

    DiskSystem& ds = DiskSystem::instance();
    Disk* disk = ds.get(file.drive);
    if (!disk || !disk->is_open()) {
        return data;
    }

    const DiskParameterBlock& dpb = disk->dpb();
    uint16_t sector_size = disk->sector_size();
    uint16_t block_size = 1 << (dpb.bsh + 7);
    uint16_t sectors_per_block = block_size / sector_size;
    std::cerr << "[SFTP] DPB: spt=" << dpb.spt << " bsh=" << (int)dpb.bsh
              << " off=" << dpb.off << " sector_size=" << sector_size << std::endl;

    // Pre-allocate result
    data.reserve(length);

    std::vector<uint8_t> block_buf(block_size);

    // Collect all blocks from all extents, in order
    std::vector<uint16_t> all_blocks;
    std::cerr << "[SFTP] read_file_data: " << file.extents.size() << " extents" << std::endl;
    for (const auto& extent : file.extents) {
        std::cerr << "[SFTP]   extent " << (int)extent.extent_num
                  << " rc=" << (int)extent.record_count
                  << " blocks=" << extent.blocks.size() << ":";
        for (uint16_t block : extent.blocks) {
            std::cerr << " " << block;
            if (block != 0) {
                all_blocks.push_back(block);
            }
        }
        std::cerr << std::endl;
    }
    std::cerr << "[SFTP]   total blocks: " << all_blocks.size()
              << " offset=" << offset << " block_size=" << block_size
              << " start_idx=" << (offset / block_size) << std::endl;

    // Calculate which blocks we need based on offset
    uint32_t start_block_idx = offset / block_size;
    uint32_t start_byte_in_block = offset % block_size;

    uint64_t bytes_read = 0;

    for (size_t bi = start_block_idx; bi < all_blocks.size() && bytes_read < length; bi++) {
        uint16_t block = all_blocks[bi];

        // Calculate absolute byte offset on disk
        // For hd1k: 2 system tracks * 16 physical sectors * 512 bytes = 16384 bytes offset
        // Block 0 starts at byte 16384
        uint32_t byte_offset = 16384 + block * block_size;

        // Read the block (multiple sectors)
        // For hd1k: 16 physical sectors per track, 512 bytes each = 8KB per track
        bool block_valid = true;
        for (uint16_t si = 0; si < sectors_per_block; si++) {
            uint32_t byte_pos = byte_offset + si * sector_size;
            uint16_t track = byte_pos / 8192;  // 8KB per track
            uint16_t sector = (byte_pos % 8192) / 512;  // 0-based sector

            if (si == 0) {
                std::cerr << "[SFTP] read block " << block << " -> track " << track
                          << " sector " << sector << " (byte " << byte_pos << ")" << std::endl;
            }

            disk->set_track(track);
            disk->set_sector(sector);

            if (disk->read_sector(&block_buf[si * sector_size]) != 0) {
                block_valid = false;
                break;
            }
        }

        if (!block_valid) {
            break;
        }

        // Determine start position in this block
        uint32_t start_in_block = (bi == start_block_idx) ? start_byte_in_block : 0;

        // Copy relevant portion of block to output
        for (uint32_t i = start_in_block; i < block_size && bytes_read < length; i++) {
            data.push_back(block_buf[i]);
            bytes_read++;
        }
    }

    return data;
}
