// console.cpp - Console management implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "console.h"
#include <iostream>

Console::Console(int id)
    : id_(id)
    , connected_(false)
    , local_mode_(false)
{
}

uint8_t Console::const_status() {
    // Check if input available - always check queue regardless of connection state
    // (Matches SIMH approach: status based on queue content, not connection flag)
    return input_queue_.available() > 0 ? 0xFF : 0x00;
}

uint8_t Console::read_char() {
    // Read from queue regardless of connection state
    // (Matches SIMH approach: I/O based on queue content, not connection flag)
    // Brief wait - MP/M should poll with CONST first
    int ch = input_queue_.read(10);  // 10ms timeout
    return ch >= 0 ? static_cast<uint8_t>(ch) : 0x00;
}

void Console::write_char(uint8_t ch) {
    // Always queue output for SSH transmission (even before connect)
    // This allows boot messages to be read when SSH connects
    output_queue_.try_write(ch);

    // In local mode AND not connected: also echo to stdout
    // Once SSH connects, output only goes through the queue
    if (local_mode_.load() && !connected_.load()) {
        std::cout.put(static_cast<char>(ch));
        std::cout.flush();
    }
}

void Console::reset() {
    connected_.store(false);
    // Don't clear queues - preserve pending I/O for next connection
    // (Matches SIMH behavior where input in the queue is preserved)
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
    // MP/M II creates TMP on console (MAXCONSOLE - 1)
    // With 4 consoles, the TMP runs on console 3
    // Assign from highest active console down so first connection gets the active TMP console
    for (int i = active_consoles_ - 1; i >= 0; i--) {
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
