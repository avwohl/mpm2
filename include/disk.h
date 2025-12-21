// disk.h - Disk I/O for MP/M II
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DISK_H
#define DISK_H

#include <cstdint>
#include <string>
#include <fstream>
#include <vector>
#include <memory>

// Disk Parameter Header (DPH) - 16 bytes
struct DiskParameterHeader {
    uint16_t xlt;       // Translation table address (or 0)
    uint16_t scratch1;  // Scratch area
    uint16_t scratch2;
    uint16_t scratch3;
    uint16_t dirbuf;    // Directory buffer address
    uint16_t dpb;       // Disk Parameter Block address
    uint16_t csv;       // Checksum vector address
    uint16_t alv;       // Allocation vector address
};

// Disk Parameter Block (DPB) - 15 bytes
struct DiskParameterBlock {
    uint16_t spt;       // Sectors per track
    uint8_t  bsh;       // Block shift factor
    uint8_t  blm;       // Block mask
    uint8_t  exm;       // Extent mask
    uint16_t dsm;       // Disk size - 1 (in blocks)
    uint16_t drm;       // Directory max - 1
    uint8_t  al0;       // Allocation bitmap 0
    uint8_t  al1;       // Allocation bitmap 1
    uint16_t cks;       // Checksum size
    uint16_t off;       // Track offset
};

// Disk format types
enum class DiskFormat {
    SSSD_8,     // 8" SSSD: 77 tracks, 26 sectors, 128 bytes
    HD1K,       // RomWBW hd1k: 1024 tracks, 16 sectors, 512 bytes (8MB)
    HD512,      // RomWBW hd512: 1040 tracks, 16 sectors, 512 bytes
    CUSTOM      // Custom geometry
};

// Single disk drive
class Disk {
public:
    Disk();
    ~Disk();

    // Open disk image file
    bool open(const std::string& path, bool read_only = false);
    void close();

    bool is_open() const { return file_.is_open(); }
    bool is_read_only() const { return read_only_; }

    // Disk geometry
    void set_geometry(uint16_t sectors_per_track, uint16_t tracks,
                      uint16_t sector_size = 128);
    void set_format(DiskFormat format);
    DiskFormat detect_format() const;

    uint16_t sectors_per_track() const { return sectors_per_track_; }
    uint16_t tracks() const { return tracks_; }
    uint16_t sector_size() const { return sector_size_; }
    DiskFormat format() const { return format_; }

    // Seek to track/sector
    void set_track(uint16_t track) { current_track_ = track; }
    void set_sector(uint16_t sector) { current_sector_ = sector; }

    uint16_t current_track() const { return current_track_; }
    uint16_t current_sector() const { return current_sector_; }

    // Read/write current sector
    // Returns 0 on success, non-zero on error
    int read_sector(uint8_t* buffer);
    int write_sector(const uint8_t* buffer);

    // Get DPB for standard disk formats
    const DiskParameterBlock& dpb() const { return dpb_; }

private:
    // Calculate file offset for current track/sector
    size_t sector_offset() const;

    std::fstream file_;
    std::string path_;
    bool read_only_;

    DiskFormat format_;
    uint16_t sectors_per_track_;
    uint16_t tracks_;
    uint16_t sector_size_;

    uint16_t current_track_;
    uint16_t current_sector_;

    DiskParameterBlock dpb_;
};

// Disk subsystem - manages multiple drives
class DiskSystem {
public:
    static constexpr int MAX_DISKS = 16;

    static DiskSystem& instance();

    // Mount disk image on drive (0 = A:, 1 = B:, etc.)
    bool mount(int drive, const std::string& path, bool read_only = false);
    void unmount(int drive);

    // Get disk by drive number
    Disk* get(int drive);

    // Select current disk
    bool select(int drive);
    int current_drive() const { return current_drive_; }

    // Track/sector operations on current disk
    void set_track(uint16_t track);
    void set_sector(uint16_t sector);
    void set_dma(uint16_t addr) { dma_addr_ = addr; }

    uint16_t dma_addr() const { return dma_addr_; }

    // Read/write using DMA address and banked memory
    int read(class BankedMemory* mem);
    int write(class BankedMemory* mem);

    // Sector translation (for skewed disks)
    uint16_t translate(uint16_t logical_sector, uint16_t track);

private:
    DiskSystem();

    std::unique_ptr<Disk> disks_[MAX_DISKS];
    int current_drive_;
    uint16_t dma_addr_;
};

#endif // DISK_H
