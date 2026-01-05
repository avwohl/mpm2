// sftp_path.h - SFTP path parsing
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NOTE: All file I/O goes through the Z80 RSP bridge, NOT direct C++ disk access.

#ifndef SFTP_PATH_H
#define SFTP_PATH_H

#include <string>
#include <vector>
#include <cstdint>

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

// Get list of mounted drives (for root listing)
std::vector<int> get_mounted_drives();

#endif // SFTP_PATH_H
