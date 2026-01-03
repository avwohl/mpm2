// console.h - Console management for MP/M II terminals
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CONSOLE_H
#define CONSOLE_H

#include "console_queue.h"
#include <atomic>
#include <array>

// Maximum number of consoles supported
// This is the emulator's capacity. Active console count (from MP/M SYSDAT)
// is tracked separately in ConsoleManager::active_consoles_
constexpr int MAX_CONSOLES = 8;

// Console state for one terminal
class Console {
public:
    explicit Console(int id);

    // Non-copyable, non-movable
    Console(const Console&) = delete;
    Console& operator=(const Console&) = delete;

    int id() const { return id_; }

    // Connection state
    bool is_connected() const { return connected_.load(); }
    void set_connected(bool c) { connected_.store(c); }

    // Local console mode (outputs to stdout)
    bool is_local() const { return local_mode_.load(); }
    void set_local_mode(bool l) { local_mode_.store(l); }

    // I/O queues (accessed from multiple threads)
    ConsoleQueue<256>& input_queue() { return input_queue_; }
    ConsoleQueue<4096>& output_queue() { return output_queue_; }

    // XIOS interface (called from Z80 thread)
    // Returns 0xFF if input available, 0x00 if not
    uint8_t const_status();

    // Read character (may block briefly)
    uint8_t read_char();

    // Write character
    void write_char(uint8_t ch);

    // Reset on disconnect
    void reset();

private:
    int id_;
    std::atomic<bool> connected_;
    std::atomic<bool> local_mode_;

    ConsoleQueue<256> input_queue_;    // SSH -> Z80 (keyboard)
    ConsoleQueue<4096> output_queue_;  // Z80 -> SSH (display)
};

// Global console manager
class ConsoleManager {
public:
    static ConsoleManager& instance();

    // Initialize all consoles
    void init();

    // Get console by ID (0 to MAX_CONSOLES-1)
    Console* get(int id);

    // Find a free (disconnected) console, returns nullptr if none
    Console* find_free();

    // Number of currently connected consoles
    int connected_count() const;

    // Maximum console number (from MP/M SYSDAT)
    int max_console() const { return active_consoles_; }

    // Set active console count (from MP/M SYSDAT byte 1)
    void set_active_consoles(int count) {
        if (count > 0 && count <= MAX_CONSOLES) {
            active_consoles_ = count;
        }
    }

private:
    ConsoleManager() = default;
    std::array<std::unique_ptr<Console>, MAX_CONSOLES> consoles_;
    bool initialized_ = false;
    int active_consoles_ = 4;  // Default to 4 consoles (typical MP/M II config)
};

#endif // CONSOLE_H
