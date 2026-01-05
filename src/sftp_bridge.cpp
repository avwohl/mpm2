// sftp_bridge.cpp - SFTP request/reply bridge implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "sftp_bridge.h"
#include <cstring>
#include <algorithm>

SftpBridge& SftpBridge::instance() {
    static SftpBridge instance;
    return instance;
}

// Parse CP/M filename into 8.3 components
// Handles wildcards: * expands to fill with ?, ? matches any single char
static void parse_filename(const std::string& name,
                           uint8_t* fname8, uint8_t* ext3) {
    // Initialize with spaces
    std::memset(fname8, ' ', 8);
    std::memset(ext3, ' ', 3);

    size_t dot = name.find('.');
    std::string base = (dot != std::string::npos) ? name.substr(0, dot) : name;
    std::string ext = (dot != std::string::npos) ? name.substr(dot + 1) : "";

    // Copy base name, handle wildcards
    bool fill_rest = false;
    for (size_t i = 0; i < 8; i++) {
        if (fill_rest) {
            fname8[i] = '?';
        } else if (i < base.size()) {
            if (base[i] == '*') {
                fname8[i] = '?';
                fill_rest = true;
            } else {
                fname8[i] = std::toupper(static_cast<unsigned char>(base[i]));
            }
        }
    }

    // Copy extension, handle wildcards
    fill_rest = false;
    for (size_t i = 0; i < 3; i++) {
        if (fill_rest) {
            ext3[i] = '?';
        } else if (i < ext.size()) {
            if (ext[i] == '*') {
                ext3[i] = '?';
                fill_rest = true;
            } else {
                ext3[i] = std::toupper(static_cast<unsigned char>(ext[i]));
            }
        }
    }
}

void SftpRequest::serialize(uint8_t* buf, size_t buf_size) const {
    if (buf_size < SFTP_BUF_SIZE) return;

    std::memset(buf, 0, buf_size);

    buf[0] = static_cast<uint8_t>(type);
    buf[1] = drive;
    buf[2] = user;
    buf[3] = flags;

    // Parse and store filename
    parse_filename(filename, &buf[SFTP_FILENAME_OFS], &buf[SFTP_EXT_OFS]);

    // Store offset (32-bit little-endian split into two 16-bit words)
    buf[SFTP_OFFSET_OFS] = offset & 0xFF;
    buf[SFTP_OFFSET_OFS + 1] = (offset >> 8) & 0xFF;
    buf[SFTP_OFFSET_OFS + 2] = (offset >> 16) & 0xFF;
    buf[SFTP_OFFSET_OFS + 3] = (offset >> 24) & 0xFF;

    // Store length (16-bit little-endian)
    buf[SFTP_LENGTH_OFS] = length & 0xFF;
    buf[SFTP_LENGTH_OFS + 1] = (length >> 8) & 0xFF;

    // Copy data for write requests
    size_t copy_len = std::min(data.size(), SFTP_MAX_DATA);
    if (copy_len > 0) {
        std::memcpy(&buf[SFTP_DATA_OFS], data.data(), copy_len);
    }
}

SftpReply SftpReply::deserialize(const uint8_t* buf, size_t buf_size) {
    SftpReply reply;
    reply.request_id = 0;  // Filled in by caller
    reply.status = SftpReplyStatus::ERROR_INVALID;
    reply.more_data = false;

    if (buf_size < SFTP_REPLY_DATA_OFS) return reply;

    uint8_t status_byte = buf[SFTP_REPLY_STATUS_OFS];
    reply.more_data = (status_byte & 0x80) != 0;
    reply.status = static_cast<SftpReplyStatus>(status_byte & 0x7F);

    uint16_t length = buf[SFTP_REPLY_LENGTH_OFS] |
                      (buf[SFTP_REPLY_LENGTH_OFS + 1] << 8);

    size_t data_len = std::min(static_cast<size_t>(length),
                               buf_size - SFTP_REPLY_DATA_OFS);
    if (data_len > 0) {
        reply.data.assign(&buf[SFTP_REPLY_DATA_OFS],
                          &buf[SFTP_REPLY_DATA_OFS + data_len]);
    }

    return reply;
}

uint32_t SftpBridge::enqueue_request(SftpRequest req) {
    std::lock_guard<std::mutex> lock(mutex_);
    req.id = next_request_id_++;
    pending_requests_.push(std::move(req));
    return req.id;
}

std::optional<SftpReply> SftpBridge::wait_for_reply(uint32_t request_id,
                                                     int timeout_ms) {
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        // Check for matching reply (brief lock)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::queue<SftpReply> temp;
            std::optional<SftpReply> result;

            while (!pending_replies_.empty()) {
                SftpReply reply = std::move(pending_replies_.front());
                pending_replies_.pop();
                if (reply.request_id == request_id) {
                    result = std::move(reply);
                } else {
                    temp.push(std::move(reply));
                }
            }
            pending_replies_ = std::move(temp);

            if (result) return result;
        }

        // Run Z80 to allow RSP to process (without holding lock)
        if (z80_tick_) {
            z80_tick_();
        }
        // If no callback, we just spin - caller should set callback
    }

    return std::nullopt;  // Timeout
}

std::optional<SftpReply> SftpBridge::try_get_reply(uint32_t request_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check for matching reply (non-blocking)
    std::queue<SftpReply> temp;
    std::optional<SftpReply> result;

    while (!pending_replies_.empty()) {
        SftpReply reply = std::move(pending_replies_.front());
        pending_replies_.pop();
        if (reply.request_id == request_id) {
            result = std::move(reply);
        } else {
            temp.push(std::move(reply));
        }
    }
    pending_replies_ = std::move(temp);

    return result;
}

bool SftpBridge::has_pending_request() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_requests_.empty();
}

bool SftpBridge::get_request(uint8_t* buf, size_t buf_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (pending_requests_.empty()) {
        return false;
    }

    current_request_ = std::move(pending_requests_.front());
    pending_requests_.pop();

    current_request_->serialize(buf, buf_size);
    return true;
}

void SftpBridge::set_reply(const uint8_t* buf, size_t buf_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    SftpReply reply = SftpReply::deserialize(buf, buf_size);

    if (current_request_) {
        reply.request_id = current_request_->id;
        current_request_.reset();
    }

    pending_replies_.push(std::move(reply));
    reply_cv_.notify_all();
}

int SftpBridge::test_rsp_communication(int timeout_ms) {
    // Create a TEST request
    SftpRequest req;
    req.type = SftpRequestType::TEST;
    req.drive = 0;
    req.user = 0;
    req.flags = 0;
    req.offset = 0;
    req.length = 0;

    // Enqueue and wait for reply
    uint32_t id = enqueue_request(std::move(req));
    auto reply = wait_for_reply(id, timeout_ms);

    if (!reply) {
        return -1;  // Timeout
    }

    if (reply->status != SftpReplyStatus::OK) {
        return -2;  // Error status
    }

    if (reply->data.size() < 2) {
        return -3;  // Invalid reply data
    }

    // Return poll counter (16-bit little-endian)
    return reply->data[0] | (reply->data[1] << 8);
}
