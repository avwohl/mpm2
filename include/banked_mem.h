// banked_mem.h - Bank-switched memory for MP/M II
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BANKED_MEM_H
#define BANKED_MEM_H

#include "qkz80_mem.h"
#include <cstdint>
#include <vector>
#include <memory>

// MP/M II memory model:
// - Banked (0x0000-0xBFFF): Bank-switchable per process (48KB per bank)
// - Upper 16KB (0xC000-0xFFFF): High common area (shared by all processes)
//
// Banks are selected via SELMEMORY XIOS call.
// Bank 0 is typically the system bank.
// Banks 1-N are user memory segments.
//
// Page 0 (0x0000-0x00FF) is part of each bank, NOT shared. This is correct
// because page 0 contains per-process data (FCB at 0x5C, DMA at 0x80, etc.).
// Only the interrupt vectors (RST 0-7 at 0x00, 0x08, ..., 0x38) need to be
// the same in each bank. SYSINIT copies these from bank 0 to all other banks.

class BankedMemory : public qkz80_cpu_mem {
public:
    // Create memory with specified number of banks
    // Each bank is BANK_SIZE (48KB), plus COMMON_SIZE (16KB) common area
    // Total RAM = (num_banks * BANK_SIZE) + COMMON_SIZE
    explicit BankedMemory(int num_banks = 4);

    // qkz80_cpu_mem interface
    qkz80_uint8 fetch_mem(qkz80_uint16 addr, bool is_instruction = false) override;
    void store_mem(qkz80_uint16 addr, qkz80_uint8 byte) override;

    // Bank selection (called from XIOS SELMEMORY)
    void select_bank(uint8_t bank);
    uint8_t current_bank() const { return current_bank_; }

    // Direct bank access (for DMA, debugging)
    uint8_t read_bank(uint8_t bank, uint16_t addr) const;
    void write_bank(uint8_t bank, uint16_t addr, uint8_t byte);

    // Common area access (0x8000-0xFFFF)
    uint8_t read_common(uint16_t addr) const;
    void write_common(uint16_t addr, uint8_t byte);

    // Load data into specific bank at address
    void load(uint8_t bank, uint16_t addr, const uint8_t* data, size_t len);

    // Load data into common area
    void load_common(uint16_t addr, const uint8_t* data, size_t len);

    // Get total number of banks
    int num_banks() const { return num_banks_; }

    // Memory layout constants
    static constexpr uint16_t LOW_COMMON_SIZE = 0x0100;  // 256 bytes for page 0
    static constexpr uint16_t COMMON_BASE = 0xC000;      // High common starts here
    static constexpr uint16_t BANK_SIZE = 0xC000;        // 48KB per bank (includes low common overlay)
    static constexpr uint16_t COMMON_SIZE = 0x4000;      // 16KB high common area

private:
    int num_banks_;
    uint8_t current_bank_;

    // Memory storage: banks[i] is 48KB for bank i (0x0000-0xBFFF)
    // Page 0 is part of each bank (not shared)
    std::vector<std::unique_ptr<uint8_t[]>> banks_;

    // High common area: 16KB (0xC000-0xFFFF)
    std::unique_ptr<uint8_t[]> common_;
};

#endif // BANKED_MEM_H
