// mpm_cpu.h - Extended Z80 CPU with MP/M II I/O port handling
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef MPM_CPU_H
#define MPM_CPU_H

#include "qkz80.h"
#include <cstdint>

class BankedMemory;
class XIOS;

// I/O Port definitions for MP/M II emulator
// Following romwbw_emu pattern: single dispatch port with function code in register
namespace MpmPorts {
    constexpr uint8_t XIOS_DISPATCH = 0xE0;  // XIOS dispatch (B = function offset)
    constexpr uint8_t BANK_SELECT   = 0xE1;  // Bank select (A = bank number)
    constexpr uint8_t SIGNAL        = 0xE2;  // Signal/status port
}

// Extended Z80 CPU with MP/M II I/O port support
// Overrides port_in/port_out to route I/O through XIOS handler
class MpmCpu : public qkz80 {
public:
    MpmCpu(qkz80_cpu_mem* memory);

    // Set XIOS handler for dispatch
    void set_xios(XIOS* xios) { xios_ = xios; }

    // Set banked memory for bank switching
    void set_banked_mem(BankedMemory* mem) { banked_mem_ = mem; }

    // Override port I/O - routes through emulator handlers
    void port_out(qkz80_uint8 port, qkz80_uint8 value) override;
    qkz80_uint8 port_in(qkz80_uint8 port) override;

    // Override halt - for proper shutdown handling
    void halt(void) override;

    // Override execute for instruction tracing
    void execute(void) override;

    // Override unimplemented opcode - for better diagnostics
    void unimplemented_opcode(qkz80_uint8 opcode, qkz80_uint16 pc) override;

    // Check if CPU is halted
    bool is_halted() const { return halted_; }
    void clear_halted() { halted_ = false; }

    // Debug/statistics
    bool debug_io = false;

private:
    XIOS* xios_ = nullptr;
    BankedMemory* banked_mem_ = nullptr;
    bool halted_ = false;
    uint8_t last_xios_result_ = 0;  // Result from last XIOS dispatch (for IN instruction)

    // Handle XIOS dispatch via port 0xE0
    void handle_xios_dispatch();

    // Handle bank select via port 0xE1
    void handle_bank_select(uint8_t bank);
};

#endif // MPM_CPU_H
