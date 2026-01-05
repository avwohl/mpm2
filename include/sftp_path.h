// sftp_path.h - SFTP path parsing and CP/M directory access
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SFTP_PATH_H
#define SFTP_PATH_H

#include <string>
#include <vector>
#include <cstdint>
#include <optional>

// Parsed SFTP path components
struct SftpPath {
    int drive;              // -1 = root, 0-15 = A-P
    int user;               // -1 = all users, 0-15 = specific user
    std::string filename;   // Empty = directory, otherwise filename

    bool is_root() const { return drive < 0; }
    bool is_drive_root() const { return drive >= 0 && user < 0 && filename.empty(); }
    bool is_user_dir() const { return drive >= 0 && user >= 0 && filename.empty(); }
    bool is_file() const { return !filename.empty(); }
};

// Parse SFTP path into components
// Paths:
//   /           -> root (list drives)
//   /A          -> drive A (list user areas or all files)
//   /A/         -> drive A (list all files in user 0)
//   /A.5/       -> drive A, user 5 (list files)
//   /A/FILE.TXT -> drive A, user 0, FILE.TXT
//   /A.5/F.TXT  -> drive A, user 5, F.TXT
SftpPath parse_sftp_path(const std::string& path);

// Convert parsed path back to string
std::string sftp_path_to_string(const SftpPath& path);

// CP/M directory entry (32 bytes)
struct CpmDirEntry {
    uint8_t user;           // User number (0-15, 0xE5 = deleted)
    char filename[8];       // Filename (space padded)
    char extension[3];      // Extension (space padded, high bits = attributes)
    uint8_t extent;         // Extent number
    uint8_t s1;             // Reserved
    uint8_t s2;             // Reserved (extent high byte in CP/M 3)
    uint8_t record_count;   // Records in this extent (0-128)
    uint8_t allocation[16]; // Block allocation map

    // Helper methods
    bool is_deleted() const { return user == 0xE5; }
    bool is_system() const { return (extension[1] & 0x80) != 0; }
    bool is_read_only() const { return (extension[0] & 0x80) != 0; }
    std::string get_filename() const;  // Returns "NAME.EXT" format
    uint32_t get_size(uint16_t block_size) const;  // Approximate file size
};

// Directory entry for SFTP (aggregated across extents)
struct SftpDirEntry {
    std::string name;       // Full filename
    uint8_t user;           // User number
    uint32_t size;          // File size in bytes
    bool is_directory;      // True for virtual directories
    bool is_system;         // CP/M SYS attribute
    bool is_read_only;      // CP/M R/O attribute
};

// Read directory from disk
// Returns list of unique files (aggregated across extents)
std::vector<SftpDirEntry> read_directory(int drive, int user = -1);

// Get list of mounted drives (for root listing)
std::vector<int> get_mounted_drives();

// Get list of user areas with files on a drive
std::vector<int> get_active_users(int drive);

// Open file info structure
struct CpmFileInfo {
    int drive;
    int user;
    std::string filename;
    uint32_t size;              // Total file size in bytes
    bool is_read_only;

    // Extent information for reading
    struct Extent {
        uint8_t extent_num;     // Extent number (0, 1, 2, ...)
        uint8_t record_count;   // Records in this extent (0-128)
        std::vector<uint16_t> blocks;  // Allocation blocks
    };
    std::vector<Extent> extents;  // Sorted by extent number
};

// Find file and collect all its extents
// Returns nullopt if file not found
std::optional<CpmFileInfo> find_file(int drive, int user, const std::string& filename);

// Read file data
// offset: byte offset from start of file
// length: number of bytes to read
// Returns data read (may be less than length if EOF)
std::vector<uint8_t> read_file_data(const CpmFileInfo& file, uint64_t offset, uint32_t length);

#endif // SFTP_PATH_H
