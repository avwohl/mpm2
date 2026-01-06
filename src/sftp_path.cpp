// sftp_path.cpp - SFTP path parsing
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NOTE: All file I/O goes through the Z80 RSP bridge, NOT direct C++ disk access.

#include "sftp_path.h"
#include "disk.h"
#include <algorithm>
#include <cctype>

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
