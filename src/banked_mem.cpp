// banked_mem.cpp - Bank-switched memory implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "banked_mem.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

// External variable for tracking PC during memory writes (for debugging)
extern uint16_t g_debug_last_pc;
extern bool g_debug_enabled;

BankedMemory::BankedMemory(int num_banks)
    : num_banks_(num_banks)
    , current_bank_(0)
{
    if (num_banks < 1 || num_banks > 16) {
        throw std::invalid_argument("num_banks must be 1-16");
    }

    // Allocate banks (each 32KB)
    banks_.reserve(num_banks);
    for (int i = 0; i < num_banks; i++) {
        auto bank = std::make_unique<uint8_t[]>(BANK_SIZE);
        std::memset(bank.get(), 0, BANK_SIZE);
        banks_.push_back(std::move(bank));
    }

    // Allocate common area (16KB for NUCLEUS layout)
    common_ = std::make_unique<uint8_t[]>(COMMON_SIZE);
    std::memset(common_.get(), 0, COMMON_SIZE);
}

qkz80_uint8 BankedMemory::fetch_mem(qkz80_uint16 addr, bool is_instruction) {
    if (addr >= COMMON_BASE) {
        // Common area
        return common_[addr - COMMON_BASE];
    } else {
        // Banked area
        return banks_[current_bank_][addr];
    }
}

void BankedMemory::store_mem(qkz80_uint16 addr, qkz80_uint8 byte) {
    if (addr >= COMMON_BASE) {
        // Debug: track writes to SYSDAT+252-253 (DATAPG pointer at 0xFFFC-0xFFFD)
        if (g_debug_enabled && addr >= 0xFFFC && addr <= 0xFFFD) {
            static int datapg_write_count = 0;
            datapg_write_count++;
            if (datapg_write_count <= 20) {
                std::cerr << "[MEM] Write to 0x" << std::hex << addr
                          << " value=0x" << (int)byte
                          << " (old=0x" << (int)common_[addr - COMMON_BASE] << ")"
                          << " PC=0x" << g_debug_last_pc
                          << std::dec << " #" << datapg_write_count << "\n";
            }
        }
        // Common area
        common_[addr - COMMON_BASE] = byte;
    } else {
        // Banked area
        // PROTECT: RST 38H interrupt vector in bank 0 (0x38-0x3A)
        // After SYSINIT sets JP INTHND, MP/M II may try to clear TPA
        // which would corrupt the interrupt vector. Protect it.
        if (current_bank_ == 0 && addr >= 0x38 && addr <= 0x3A) {
            // Only allow writes if current value is 0x00 (initial setup)
            // or if writing 0xC3 (JP opcode) at 0x38
            if (banks_[0][addr] != 0x00 && !(addr == 0x38 && byte == 0xC3)) {
                // Ignore write to protected interrupt vector
                return;
            }
        }
        banks_[current_bank_][addr] = byte;
    }
}

void BankedMemory::select_bank(uint8_t bank) {
    if (bank >= num_banks_) {
        // Invalid bank - wrap or ignore?
        // For now, wrap to valid range
        bank = bank % num_banks_;
    }
    current_bank_ = bank;
}

uint8_t BankedMemory::read_bank(uint8_t bank, uint16_t addr) const {
    if (addr >= COMMON_BASE) {
        return common_[addr - COMMON_BASE];
    }
    if (bank >= num_banks_) {
        return 0xFF;  // Invalid bank
    }
    return banks_[bank][addr];
}

void BankedMemory::write_bank(uint8_t bank, uint16_t addr, uint8_t byte) {
    if (addr >= COMMON_BASE) {
        common_[addr - COMMON_BASE] = byte;
        return;
    }
    if (bank >= num_banks_) {
        return;  // Invalid bank
    }
    banks_[bank][addr] = byte;
}

uint8_t BankedMemory::read_common(uint16_t addr) const {
    if (addr < COMMON_BASE) {
        return 0xFF;  // Not in common area
    }
    return common_[addr - COMMON_BASE];
}

void BankedMemory::write_common(uint16_t addr, uint8_t byte) {
    if (addr < COMMON_BASE) {
        return;  // Not in common area
    }
    common_[addr - COMMON_BASE] = byte;
}

void BankedMemory::load(uint8_t bank, uint16_t addr, const uint8_t* data, size_t len) {
    if (bank >= num_banks_) return;

    for (size_t i = 0; i < len; i++) {
        uint16_t target = addr + i;
        if (target >= COMMON_BASE) {
            // Load to common area
            common_[target - COMMON_BASE] = data[i];
        } else {
            // Load to banked area
            banks_[bank][target] = data[i];
        }
    }
}

void BankedMemory::load_common(uint16_t addr, const uint8_t* data, size_t len) {
    if (addr < COMMON_BASE) return;

    uint16_t offset = addr - COMMON_BASE;
    for (size_t i = 0; i < len && (offset + i) < BANK_SIZE; i++) {
        common_[offset + i] = data[i];
    }
}
