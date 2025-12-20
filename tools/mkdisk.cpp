// mkdisk.cpp - Create CP/M disk image with files
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Creates an hd1k format disk image (8MB) and copies files to it.
// Format: 512-byte physical sectors, 4KB blocks, 1024 directory entries
//
// Usage: mkdisk -o disk.img file1.com file2.sys ...

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <getopt.h>

// hd1k disk parameters
constexpr int SECTOR_SIZE = 512;
constexpr int SECTORS_PER_TRACK = 16;
constexpr int TRACKS = 1024;
constexpr int RESERVED_TRACKS = 2;
constexpr int BLOCK_SIZE = 4096;
constexpr int DIR_ENTRIES = 1024;
constexpr int EXTENT_SIZE = 16384;  // EXM=1 means 2 extents per entry
constexpr int RECORDS_PER_EXTENT = 128;

// Directory entry structure
struct DirEntry {
    uint8_t status;      // 0=used, 0xE5=deleted
    char name[8];        // Filename (padded with spaces)
    char ext[3];         // Extension (padded with spaces)
    uint8_t extent;      // Extent number (low)
    uint8_t s1;          // Reserved
    uint8_t s2;          // Extent high bits
    uint8_t records;     // Record count in this extent (0-128)
    uint8_t alloc[16];   // Block allocation (16-bit values for DSM>255)
};

static_assert(sizeof(DirEntry) == 32, "Directory entry must be 32 bytes");

class HD1KDisk {
public:
    HD1KDisk() {
        // Create empty disk image
        data_.resize(SECTOR_SIZE * SECTORS_PER_TRACK * TRACKS, 0xE5);

        // Initialize directory entries as empty
        size_t dir_start = RESERVED_TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE;
        size_t dir_size = (DIR_ENTRIES * 32);
        std::fill(data_.begin() + dir_start, data_.begin() + dir_start + dir_size, 0xE5);

        // First data block after directory
        // Directory uses 8 blocks (1024 entries * 32 bytes / 4096)
        next_block_ = 8;
    }

    bool add_file(const std::string& filepath) {
        // Open source file
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            std::cerr << "Cannot open: " << filepath << "\n";
            return false;
        }

        // Read file contents
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> content(size);
        file.read(reinterpret_cast<char*>(content.data()), size);

        // Extract filename from path
        std::string name = filepath;
        size_t slash = name.find_last_of("/\\");
        if (slash != std::string::npos) {
            name = name.substr(slash + 1);
        }

        // Split into name and extension
        std::string fname, fext;
        size_t dot = name.find_last_of('.');
        if (dot != std::string::npos) {
            fname = name.substr(0, dot);
            fext = name.substr(dot + 1);
        } else {
            fname = name;
            fext = "";
        }

        // Convert to uppercase
        for (char& c : fname) c = std::toupper(c);
        for (char& c : fext) c = std::toupper(c);

        // Truncate to CP/M limits
        if (fname.length() > 8) fname = fname.substr(0, 8);
        if (fext.length() > 3) fext = fext.substr(0, 3);

        std::cout << "Adding " << fname << "." << fext << " (" << size << " bytes)\n";

        // Calculate blocks needed
        size_t records = (size + 127) / 128;
        size_t blocks_needed = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;

        // Check if we have enough space
        if (next_block_ + blocks_needed > 2039) {
            std::cerr << "Disk full!\n";
            return false;
        }

        // Allocate blocks and copy data
        std::vector<uint16_t> blocks;
        for (size_t i = 0; i < blocks_needed; i++) {
            blocks.push_back(next_block_++);
        }

        // Copy file data to allocated blocks
        size_t offset = 0;
        for (uint16_t block : blocks) {
            size_t block_offset = (RESERVED_TRACKS * SECTORS_PER_TRACK +
                                   block * (BLOCK_SIZE / SECTOR_SIZE)) * SECTOR_SIZE;
            size_t to_copy = std::min(size - offset, (size_t)BLOCK_SIZE);
            std::memcpy(&data_[block_offset], &content[offset], to_copy);
            offset += to_copy;
        }

        // Create a single directory entry with all blocks (for EXM=1)
        // With EXM=1, one entry can hold up to 8 blocks (32KB, 256 records)
        // RC represents records in the second logical extent (records - 128 if > 128)
        DirEntry* entry = find_free_dir_entry();
        if (!entry) {
            std::cerr << "Directory full!\n";
            return false;
        }

        entry->status = 0;  // User 0
        std::memset(entry->name, ' ', 8);
        std::memset(entry->ext, ' ', 3);
        std::memcpy(entry->name, fname.c_str(), fname.length());
        std::memcpy(entry->ext, fext.c_str(), fext.length());
        entry->extent = 0;  // EX = 0
        entry->s1 = 0;
        entry->s2 = 0;

        // Allocate all blocks (up to 8)
        std::memset(entry->alloc, 0, 16);
        for (size_t i = 0; i < blocks.size() && i < 8; i++) {
            entry->alloc[i * 2] = blocks[i] & 0xFF;
            entry->alloc[i * 2 + 1] = (blocks[i] >> 8) & 0xFF;
        }

        // LDRBDOS seems to use RC directly as total records
        // Even though CP/M spec says 0-128, try using actual value
        entry->records = (records > 255) ? 255 : records;

        std::cout << "  EX=0, RC=" << (int)entry->records
                  << ", blocks=" << blocks.size() << "\n";

        return true;
    }

    bool write(const std::string& filepath) {
        std::ofstream file(filepath, std::ios::binary);
        if (!file) {
            std::cerr << "Cannot create: " << filepath << "\n";
            return false;
        }

        file.write(reinterpret_cast<const char*>(data_.data()), data_.size());
        std::cout << "Created disk image: " << filepath << " (" << data_.size() << " bytes)\n";
        return true;
    }

private:
    DirEntry* find_free_dir_entry() {
        size_t dir_start = RESERVED_TRACKS * SECTORS_PER_TRACK * SECTOR_SIZE;
        for (int i = 0; i < DIR_ENTRIES; i++) {
            DirEntry* entry = reinterpret_cast<DirEntry*>(&data_[dir_start + i * 32]);
            if (entry->status == 0xE5) {
                return entry;
            }
        }
        return nullptr;
    }

    std::vector<uint8_t> data_;
    uint16_t next_block_;
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " -o output.img file1 file2 ...\n"
              << "\n"
              << "Creates an hd1k format disk image (8MB) with the specified files.\n"
              << "\n"
              << "Options:\n"
              << "  -o, --output FILE    Output disk image\n"
              << "  -h, --help           Show this help\n"
              << "\n";
}

int main(int argc, char* argv[]) {
    std::string output_file;

    static struct option long_options[] = {
        {"output", required_argument, nullptr, 'o'},
        {"help",   no_argument,       nullptr, 'h'},
        {nullptr,  0,                 nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "o:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'o':
                output_file = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (output_file.empty()) {
        std::cerr << "Error: Output file required (-o)\n";
        print_usage(argv[0]);
        return 1;
    }

    HD1KDisk disk;

    // Add files from remaining arguments
    for (int i = optind; i < argc; i++) {
        if (!disk.add_file(argv[i])) {
            return 1;
        }
    }

    if (!disk.write(output_file)) {
        return 1;
    }

    return 0;
}
