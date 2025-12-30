// console.cpp - Console management implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "console.h"
#include <iostream>

Console::Console(int id)
    : id_(id)
    , connected_(false)
    , local_mode_(false)
    , term_width_(80)
    , term_height_(24)
    , term_type_("vt100")
{
}

uint8_t Console::const_status() {
    // Check if input available - works for both connected and local mode
    if (!connected_.load() && !local_mode_.load()) return 0x00;
    return input_queue_.available() > 0 ? 0xFF : 0x00;
}

uint8_t Console::read_char() {
    // For non-connected, non-local consoles, return 0x00 (no input)
    // MP/M should use CONST to poll first; CONIN returning 0 means no char
    if (!connected_.load() && !local_mode_.load()) return 0x00;

    // Brief wait - MP/M should poll with CONST first
    int ch = input_queue_.read(10);  // 10ms timeout
    if (ch < 0) return 0x00;  // No character available

    return static_cast<uint8_t>(ch);
}

void Console::write_char(uint8_t ch) {
    if (local_mode_.load()) {
        // Local mode - output directly to stdout
        std::cout.put(static_cast<char>(ch));
        std::cout.flush();
        return;
    }

    // Always queue output for SSH transmission (even before connect)
    // This allows boot messages to be read when SSH connects
    output_queue_.try_write(ch);

    // Console 0: also echo to stdout for boot messages
    if (id_ == 0 && !connected_.load()) {
        std::cout.put(static_cast<char>(ch));
        std::cout.flush();
    }
}

void Console::reset() {
    connected_.store(false);
    input_queue_.clear();
    output_queue_.clear();
    term_width_.store(80);
    term_height_.store(24);
    {
        std::lock_guard<std::mutex> lock(term_mutex_);
        term_type_ = "vt100";
    }
}

// ConsoleManager implementation

ConsoleManager& ConsoleManager::instance() {
    static ConsoleManager instance;
    return instance;
}

void ConsoleManager::init() {
    if (initialized_) return;

    for (int i = 0; i < MAX_CONSOLES; i++) {
        consoles_[i] = std::make_unique<Console>(i);
    }
    initialized_ = true;
}

Console* ConsoleManager::get(int id) {
    if (id < 0 || id >= MAX_CONSOLES) return nullptr;
    return consoles_[id].get();
}

Console* ConsoleManager::find_free() {
    // MP/M II typically has 4 consoles (0-3), with TMP running on console 3
    // Assign from highest down so first connection gets console 3 where TMP is active
    constexpr int MPM_MAX_CONSOLES = 4;  // MP/M II typically uses 4 consoles
    for (int i = MPM_MAX_CONSOLES - 1; i >= 0; i--) {
        if (consoles_[i] && !consoles_[i]->is_connected()) {
            return consoles_[i].get();
        }
    }
    return nullptr;
}

int ConsoleManager::connected_count() const {
    int count = 0;
    for (const auto& con : consoles_) {
        if (con && con->is_connected()) count++;
    }
    return count;
}
