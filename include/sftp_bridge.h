// sftp_bridge.h - SFTP request/reply bridge between C++ and Z80 RSP
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SFTP_BRIDGE_H
#define SFTP_BRIDGE_H

#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <optional>

// SFTP request types (sent to Z80)
enum class SftpRequestType : uint8_t {
    DIR_SEARCH = 0,    // Directory search (BDOS 17/18)
    FILE_READ = 1,     // Read file data (BDOS 20)
    FILE_WRITE = 2,    // Write file data (BDOS 21)
    FILE_DELETE = 3,   // Delete file (BDOS 19)
    FILE_CREATE = 4,   // Create file (BDOS 22)
    FILE_CLOSE = 5,    // Close file (BDOS 16)
    FILE_OPEN = 6,     // Open file (BDOS 15)
    TEST = 255,        // Test - returns poll counter
};

// SFTP reply status codes (from Z80)
enum class SftpReplyStatus : uint8_t {
    OK = 0,
    ERROR_NOT_FOUND = 1,
    ERROR_DISK_FULL = 2,
    ERROR_READ_ONLY = 3,
    ERROR_INVALID = 4,
    ERROR_EXISTS = 5,
    MORE_DATA = 0x80,  // OR'd with status if more data pending
};

// Request structure for Z80 buffer (packed for wire format)
// Buffer layout (max 256 bytes in common memory):
//   [0]     type (SftpRequestType)
//   [1]     drive (0=A, 1=B, ...)
//   [2]     user (0-15)
//   [3]     flags (search: 0=first, 1=next; open: bit0=create)
//   [4-11]  filename (8 bytes, space padded)
//   [12-14] extension (3 bytes, space padded)
//   [15-16] offset_low (for read/write)
//   [17-18] offset_high
//   [19-20] length (for read/write)
//   [21+]   data (for write)
constexpr size_t SFTP_BUF_SIZE = 256;
constexpr size_t SFTP_FILENAME_OFS = 4;
constexpr size_t SFTP_EXT_OFS = 12;
constexpr size_t SFTP_OFFSET_OFS = 15;
constexpr size_t SFTP_LENGTH_OFS = 19;
constexpr size_t SFTP_DATA_OFS = 21;
constexpr size_t SFTP_MAX_DATA = SFTP_BUF_SIZE - SFTP_DATA_OFS;

// Reply structure from Z80 buffer
// Buffer layout:
//   [0]     status (SftpReplyStatus, bit 7 = more data)
//   [1-2]   length (data bytes following)
//   [3+]    data (directory entries or file data)
constexpr size_t SFTP_REPLY_STATUS_OFS = 0;
constexpr size_t SFTP_REPLY_LENGTH_OFS = 1;
constexpr size_t SFTP_REPLY_DATA_OFS = 3;

// Directory entry in reply (32 bytes, CP/M FCB format)
// [0]     user number
// [1-8]   filename (space padded)
// [9-11]  extension (space padded, high bits = attributes)
// [12]    extent number
// [13-14] reserved
// [15]    record count
// [16-31] allocation map
constexpr size_t SFTP_DIRENT_SIZE = 32;

// High-level request structure (C++ side)
struct SftpRequest {
    uint32_t id;                // Request ID for matching replies
    SftpRequestType type;
    uint8_t drive;              // 0=A, 1=B, etc.
    uint8_t user;               // User area 0-15
    uint8_t flags;              // Type-specific flags
    std::string filename;       // CP/M 8.3 format
    uint32_t offset;            // File offset for read/write
    uint16_t length;            // Requested length
    std::vector<uint8_t> data;  // Write data

    // Serialize to Z80 buffer format
    void serialize(uint8_t* buf, size_t buf_size) const;
};

// High-level reply structure (C++ side)
struct SftpReply {
    uint32_t request_id;        // Matching request ID
    SftpReplyStatus status;
    bool more_data;             // More data available
    std::vector<uint8_t> data;  // Response data

    // Deserialize from Z80 buffer format
    static SftpReply deserialize(const uint8_t* buf, size_t buf_size);
};

// Bridge class for managing request/reply queue
// Thread-safe for C++ SSH handlers to enqueue requests
class SftpBridge {
public:
    static SftpBridge& instance();

    // C++ side (called from SSH handler thread)
    uint32_t enqueue_request(SftpRequest req);
    std::optional<SftpReply> wait_for_reply(uint32_t request_id,
                                            int timeout_ms = 5000);

    // Test function - sends TEST request and returns poll counter, -1 on error
    int test_rsp_communication(int timeout_ms = 5000);

    // Z80 side (called from XIOS handlers in main thread)
    bool has_pending_request() const;
    bool get_request(uint8_t* buf, size_t buf_size);
    void set_reply(const uint8_t* buf, size_t buf_size);

private:
    SftpBridge() = default;

    mutable std::mutex mutex_;
    std::condition_variable reply_cv_;

    std::queue<SftpRequest> pending_requests_;
    std::optional<SftpRequest> current_request_;
    std::queue<SftpReply> pending_replies_;

    uint32_t next_request_id_ = 1;
};

#endif // SFTP_BRIDGE_H
