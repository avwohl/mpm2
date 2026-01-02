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
// - Lower 48KB (0x0000-0xBFFF): Bank-switchable per process
// - Upper 16KB (0xC000-0xFFFF): Common area (shared by all processes)
//
// Banks are selected via SELMEMORY XIOS call.
// Bank 0 is typically the system bank.
// Banks 1-N are user memory segments.

class BankedMemory : public qkz80_cpu_mem {
public:
    // Create memory with specified number of banks
    // Each bank is 32KB, plus 32KB common area
    // Total RAM = (num_banks * 32KB) + 32KB common
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

    // Common base address - NUCLEUS uses C000
    static constexpr uint16_t COMMON_BASE = 0xC000;
    static constexpr uint16_t BANK_SIZE = 0xC000;  // 48KB per bank
    static constexpr uint16_t COMMON_SIZE = 0x4000;  // 16KB common area

private:
    int num_banks_;
    uint8_t current_bank_;

    // Memory storage: banks[i] is 48KB for bank i (0x0000-0xBFFF)
    std::vector<std::unique_ptr<uint8_t[]>> banks_;

    // Common area: 16KB (0xC000-0xFFFF)
    std::unique_ptr<uint8_t[]> common_;
};

#endif // BANKED_MEM_H
