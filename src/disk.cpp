// disk.cpp - Disk I/O implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "disk.h"
#include "banked_mem.h"
#include <cstring>
#include <iostream>
#include <iomanip>

Disk::Disk()
    : read_only_(false)
    , format_(DiskFormat::SSSD_8)
    , sectors_per_track_(26)
    , tracks_(77)
    , sector_size_(128)
    , current_track_(0)
    , current_sector_(1)
{
    // Default DPB for standard 8" SSSD floppy
    set_format(DiskFormat::SSSD_8);
}

Disk::~Disk() {
    close();
}

bool Disk::open(const std::string& path, bool read_only) {
    close();

    path_ = path;
    read_only_ = read_only;

    auto mode = std::ios::binary | std::ios::in;
    if (!read_only) {
        mode |= std::ios::out;
    }

    file_.open(path, mode);
    if (!file_.is_open() && !read_only) {
        // Try read-only if read-write failed
        file_.open(path, std::ios::binary | std::ios::in);
        if (file_.is_open()) {
            read_only_ = true;
        }
    }

    return file_.is_open();
}

void Disk::close() {
    if (file_.is_open()) {
        file_.close();
    }
}

void Disk::set_geometry(uint16_t spt, uint16_t trk, uint16_t sec_size) {
    format_ = DiskFormat::CUSTOM;
    sectors_per_track_ = spt;
    tracks_ = trk;
    sector_size_ = sec_size;

    // Update DPB based on geometry
    dpb_.spt = spt;
    // Other DPB fields would need to be calculated based on disk format
}

void Disk::set_format(DiskFormat format) {
    format_ = format;

    switch (format) {
        case DiskFormat::SSSD_8:
            // Standard 8" SSSD floppy: 77 tracks, 26 sectors, 128 bytes
            sectors_per_track_ = 26;
            tracks_ = 77;
            sector_size_ = 128;
            dpb_.spt = 26;
            dpb_.bsh = 3;       // 1K blocks
            dpb_.blm = 7;
            dpb_.exm = 0;
            dpb_.dsm = 242;     // 243 blocks
            dpb_.drm = 63;      // 64 directory entries
            dpb_.al0 = 0xC0;
            dpb_.al1 = 0x00;
            dpb_.cks = 16;
            dpb_.off = 2;       // 2 system tracks
            break;

        case DiskFormat::HD1K:
            // RomWBW hd1k: 1024 tracks, 16 sectors, 512 bytes = 8MB
            sectors_per_track_ = 16;
            tracks_ = 1024;
            sector_size_ = 512;
            dpb_.spt = 64;      // 16 sectors * 512 bytes = 8K per track, /128 = 64 logical sectors
            dpb_.bsh = 5;       // 4K blocks (2^5 * 128 = 4096)
            dpb_.blm = 31;
            dpb_.exm = 1;
            dpb_.dsm = 2039;    // 2040 blocks (8MB - 2 tracks) / 4K
            dpb_.drm = 1023;    // 1024 directory entries
            dpb_.al0 = 0xFF;
            dpb_.al1 = 0xFF;
            dpb_.cks = 0;       // No checksum (fixed disk)
            dpb_.off = 2;       // 2 system tracks
            break;

        case DiskFormat::HD512:
            // RomWBW hd512: 1040 tracks, 16 sectors, 512 bytes = 8.32MB
            sectors_per_track_ = 16;
            tracks_ = 1040;
            sector_size_ = 512;
            dpb_.spt = 64;
            dpb_.bsh = 5;
            dpb_.blm = 31;
            dpb_.exm = 1;
            dpb_.dsm = 2047;
            dpb_.drm = 511;     // 512 directory entries
            dpb_.al0 = 0xFF;
            dpb_.al1 = 0x00;
            dpb_.cks = 0;
            dpb_.off = 16;      // 16 system tracks
            break;

        case DiskFormat::CUSTOM:
            // Don't change anything
            break;
    }
}

DiskFormat Disk::detect_format() const {
    if (!file_.is_open()) return DiskFormat::SSSD_8;

    // Get file size
    auto& f = const_cast<std::fstream&>(file_);
    auto pos = f.tellg();
    f.seekg(0, std::ios::end);
    size_t size = f.tellg();
    f.seekg(pos);

    // hd1k: exactly 8MB (8,388,608 bytes)
    if (size == 8388608) {
        return DiskFormat::HD1K;
    }

    // hd512: 8.32MB (8,519,680 bytes)
    if (size == 8519680) {
        return DiskFormat::HD512;
    }

    // 8" SSSD: ~250KB
    if (size <= 256256) {
        return DiskFormat::SSSD_8;
    }

    // Default to hd1k for large disks
    if (size >= 8000000) {
        return DiskFormat::HD1K;
    }

    return DiskFormat::SSSD_8;
}

size_t Disk::sector_offset() const {
    // Calculate byte offset in image file
    // LDRBIOS uses 0-based sector numbers, so no translation needed
    size_t offset = (current_track_ * sectors_per_track_ + current_sector_) * sector_size_;
    return offset;
}

int Disk::read_sector(uint8_t* buffer) {
    if (!file_.is_open()) return 1;

    size_t offset = sector_offset();
    file_.seekg(offset, std::ios::beg);

    if (!file_.good()) {
        // Beyond end of file - return empty sector
        std::memset(buffer, 0xE5, sector_size_);
        return 0;
    }

    file_.read(reinterpret_cast<char*>(buffer), sector_size_);

    if (file_.gcount() < sector_size_) {
        // Partial read - pad with E5
        std::memset(buffer + file_.gcount(), 0xE5, sector_size_ - file_.gcount());
    }

    return 0;
}

int Disk::write_sector(const uint8_t* buffer) {
    if (!file_.is_open()) return 1;
    if (read_only_) return 1;

    size_t offset = sector_offset();
    file_.seekp(offset, std::ios::beg);
    file_.write(reinterpret_cast<const char*>(buffer), sector_size_);
    file_.flush();

    return file_.good() ? 0 : 1;
}

// DiskSystem implementation

DiskSystem& DiskSystem::instance() {
    static DiskSystem instance;
    return instance;
}

DiskSystem::DiskSystem()
    : current_drive_(0)
    , dma_addr_(0x0080)
{
}

bool DiskSystem::mount(int drive, const std::string& path, bool read_only) {
    if (drive < 0 || drive >= MAX_DISKS) return false;

    disks_[drive] = std::make_unique<Disk>();
    if (!disks_[drive]->open(path, read_only)) {
        disks_[drive].reset();
        return false;
    }

    // Auto-detect and set disk format
    DiskFormat format = disks_[drive]->detect_format();
    disks_[drive]->set_format(format);

    return true;
}

void DiskSystem::unmount(int drive) {
    if (drive >= 0 && drive < MAX_DISKS) {
        disks_[drive].reset();
    }
}

Disk* DiskSystem::get(int drive) {
    if (drive < 0 || drive >= MAX_DISKS) return nullptr;
    return disks_[drive].get();
}

bool DiskSystem::select(int drive) {
    if (drive < 0 || drive >= MAX_DISKS) return false;
    if (!disks_[drive]) return false;

    current_drive_ = drive;
    return true;
}

void DiskSystem::set_track(uint16_t track) {
    Disk* disk = get(current_drive_);
    if (disk) disk->set_track(track);
}

void DiskSystem::set_sector(uint16_t sector) {
    Disk* disk = get(current_drive_);
    if (disk) disk->set_sector(sector);
}

int DiskSystem::read(BankedMemory* mem) {
    Disk* disk = get(current_drive_);
    if (!disk || !disk->is_open()) return 1;

    // CP/M BIOS operates with 128-byte logical sectors
    // For hd1k: 64 logical sectors/track (128 bytes each) = 8KB/track
    // Physical: 16 sectors/track (512 bytes each) = 8KB/track
    // So logical sector N maps to physical sector N/4, offset (N%4)*128

    uint16_t logical_sector = disk->current_sector();
    uint16_t track = disk->current_track();
    uint16_t phys_sector_size = disk->sector_size();

    // Apply sector skew translation for formats that use it (e.g., ibm-3740)
    uint16_t translated_sector = translate(logical_sector, track);

    // Calculate physical sector and offset within it
    uint16_t records_per_phys = phys_sector_size / 128;  // 4 for 512-byte sectors
    uint16_t phys_sector = translated_sector / records_per_phys;  // 0-based (disk image is 0-indexed)
    uint16_t offset_in_phys = (translated_sector % records_per_phys) * 128;

    // Debug tracing for FC00 area
    bool trace_this = (dma_addr_ >= 0xFC00 && dma_addr_ < 0xFD00);
    bool trace_dir = false;

    // Temporarily set physical sector for reading
    disk->set_sector(phys_sector);

    uint8_t buffer[1024];  // Max sector size
    int result = disk->read_sector(buffer);


    // Restore logical sector (for consistency)
    disk->set_sector(logical_sector);

    if (result == 0) {
        // Copy only 128 bytes (one CP/M record) to memory at DMA address
        for (uint16_t i = 0; i < 128; i++) {
            mem->store_mem(dma_addr_ + i, buffer[offset_in_phys + i]);
        }

        // Trace key addresses
        if (trace_this || trace_dir) {
            std::cerr << "[DISK READ] trk=" << std::dec << track
                      << " logsec=" << logical_sector
                      << " xlat=" << translated_sector
                      << " physec=" << phys_sector
                      << " off=" << offset_in_phys
                      << " dma=0x" << std::hex << dma_addr_
                      << " first bytes: ";
            for (int i = 0; i < 8; i++) {
                std::cerr << std::hex << std::setw(2) << std::setfill('0')
                          << (int)buffer[offset_in_phys + i] << " ";
            }
            std::cerr << std::dec << std::endl;
            // Verify the data was actually written
            std::cerr << "[DISK READ] Verifying at 0x" << std::hex << dma_addr_ << " after store: ";
            for (int i = 0; i < 8; i++) {
                std::cerr << std::hex << std::setw(2) << std::setfill('0')
                          << (int)mem->fetch_mem(dma_addr_ + i) << " ";
            }
            std::cerr << std::dec << std::endl;
            // Also show MPM.SYS entry position (32 bytes in)
            if (trace_dir) {
                std::cerr << "[DISK READ] MPM.SYS should be at offset 32: ";
                for (int i = 0; i < 16; i++) {
                    std::cerr << std::hex << std::setw(2) << std::setfill('0')
                              << (int)buffer[offset_in_phys + 32 + i] << " ";
                }
                std::cerr << std::dec << std::endl;
            }
        }
    }

    return result;
}

int DiskSystem::write(BankedMemory* mem) {
    Disk* disk = get(current_drive_);
    if (!disk || !disk->is_open()) return 1;

    // CP/M BIOS operates with 128-byte logical sectors
    // For writes, we need to read-modify-write the physical sector

    uint16_t logical_sector = disk->current_sector();
    uint16_t track = disk->current_track();
    uint16_t phys_sector_size = disk->sector_size();

    // Apply sector skew translation for formats that use it (e.g., ibm-3740)
    uint16_t translated_sector = translate(logical_sector, track);

    // Calculate physical sector and offset within it
    uint16_t records_per_phys = phys_sector_size / 128;  // 4 for 512-byte sectors
    uint16_t phys_sector = translated_sector / records_per_phys;  // 0-based (disk image is 0-indexed)
    uint16_t offset_in_phys = (translated_sector % records_per_phys) * 128;

    uint8_t buffer[1024];  // Max sector size

    // Read existing physical sector
    disk->set_sector(phys_sector);
    disk->read_sector(buffer);

    // Modify the 128-byte portion
    for (uint16_t i = 0; i < 128; i++) {
        buffer[offset_in_phys + i] = mem->fetch_mem(dma_addr_ + i);
    }

    // Write back the physical sector
    int result = disk->write_sector(buffer);

    // Restore logical sector
    disk->set_sector(logical_sector);

    return result;
}

// Standard CP/M skew table for ibm-3740 (26 sectors, skew factor 6)
// These tables map between logical sector numbers and physical positions in disk images.
// Disk images created by cpmtools with "skew 6" store sectors in interleaved order.
// When reading logical sector L, we read from physical position log_to_phys[L].

// log_to_phys[logical] = physical position in disk image
static const uint8_t skew_log_to_phys[26] = {
    0, 6, 12, 18, 24, 4, 10, 16, 22, 2, 8, 14, 20, 1, 7, 13, 19, 25, 5, 11, 17, 23, 3, 9, 15, 21
};

// phys_to_log[physical] = logical sector (inverse of above)
static const uint8_t skew_phys_to_log[26] = {
    0, 13, 9, 22, 5, 18, 1, 14, 10, 23, 6, 19, 2, 15, 11, 24, 7, 20, 3, 16, 12, 25, 8, 21, 4, 17
};

uint16_t DiskSystem::translate(uint16_t logical_sector, uint16_t track) {
    // Check disk format - skew only applies to ibm-3740 (SSSD_8)
    Disk* disk = get(current_drive_);
    if (!disk) return logical_sector;

    // Only apply skew for ibm-3740 format
    // The MPMII_1.img disk image stores sectors in PHYSICAL order with skew
    if (disk->format() == DiskFormat::SSSD_8) {
        // ibm-3740 uses skew factor 6
        // log_to_phys[L] = physical position where logical sector L is stored
        if (logical_sector < 26) {
            return skew_log_to_phys[logical_sector];
        }
    }

    (void)track;
    (void)skew_phys_to_log;  // Kept for reference
    return logical_sector;
}
