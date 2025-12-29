// banked_mem.cpp - Bank-switched memory implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "banked_mem.h"
#include <cstring>
#include <stdexcept>
#include <iostream>

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
    // Trace writes to TMPD area (FE00-FEFF)
    static int tmpd_write_count = 0;
    if (addr >= 0xFE00 && addr < 0xFF00) {
        tmpd_write_count++;
        if (tmpd_write_count <= 100) {
            std::cerr << "[TMPD WRITE #" << tmpd_write_count << "] addr=0x"
                      << std::hex << addr << " byte=0x" << (int)byte
                      << std::dec << "\n";
        }
    }

    if (addr >= COMMON_BASE) {
        // Common area
        common_[addr - COMMON_BASE] = byte;
    } else {
        // Banked area
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
