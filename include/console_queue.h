// console_queue.h - Thread-safe character queue for console I/O
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CONSOLE_QUEUE_H
#define CONSOLE_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>

// Thread-safe character queue for console I/O
template<size_t CAPACITY = 256>
class ConsoleQueue {
public:
    ConsoleQueue() = default;

    // Non-copyable
    ConsoleQueue(const ConsoleQueue&) = delete;
    ConsoleQueue& operator=(const ConsoleQueue&) = delete;

    // Number of characters available for reading
    size_t available() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

    // Space available for writing
    size_t space() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return CAPACITY - queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }

    bool full() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size() >= CAPACITY;
    }

    // Non-blocking read: returns -1 if empty
    int try_read() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.empty()) return -1;
        uint8_t ch = queue_.front();
        queue_.pop();
        not_full_.notify_one();
        return ch;
    }

    // Blocking read with timeout (milliseconds)
    // Returns -1 on timeout, character value otherwise
    int read(unsigned timeout_ms = 0) {
        std::unique_lock<std::mutex> lock(mtx_);

        if (timeout_ms == 0) {
            // Wait indefinitely
            not_empty_.wait(lock, [this] { return !queue_.empty(); });
        } else {
            // Wait with timeout
            auto deadline = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(timeout_ms);
            if (!not_empty_.wait_until(lock, deadline, [this] { return !queue_.empty(); })) {
                return -1;  // Timeout
            }
        }

        uint8_t ch = queue_.front();
        queue_.pop();
        not_full_.notify_one();
        return ch;
    }

    // Non-blocking write: returns false if full
    bool try_write(uint8_t ch) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.size() >= CAPACITY) return false;
        queue_.push(ch);
        not_empty_.notify_one();
        return true;
    }

    // Blocking write with timeout (milliseconds)
    // Returns false on timeout
    bool write(uint8_t ch, unsigned timeout_ms = 0) {
        std::unique_lock<std::mutex> lock(mtx_);

        if (timeout_ms == 0) {
            // Wait indefinitely
            not_full_.wait(lock, [this] { return queue_.size() < CAPACITY; });
        } else {
            // Wait with timeout
            auto deadline = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(timeout_ms);
            if (!not_full_.wait_until(lock, deadline,
                    [this] { return queue_.size() < CAPACITY; })) {
                return false;  // Timeout
            }
        }

        queue_.push(ch);
        not_empty_.notify_one();
        return true;
    }

    // Write multiple characters, returns count written
    size_t write_some(const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(mtx_);
        size_t count = 0;
        while (count < len && queue_.size() < CAPACITY) {
            queue_.push(data[count++]);
        }
        if (count > 0) not_empty_.notify_one();
        return count;
    }

    // Read multiple characters, returns count read
    size_t read_some(uint8_t* data, size_t max_len) {
        std::lock_guard<std::mutex> lock(mtx_);
        size_t count = 0;
        while (count < max_len && !queue_.empty()) {
            data[count++] = queue_.front();
            queue_.pop();
        }
        if (count > 0) not_full_.notify_one();
        return count;
    }

    // Clear all queued data
    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        while (!queue_.empty()) queue_.pop();
        not_full_.notify_all();
    }

private:
    std::queue<uint8_t> queue_;
    mutable std::mutex mtx_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
};

#endif // CONSOLE_QUEUE_H
