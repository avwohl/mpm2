// mpm_cpu.cpp - Extended Z80 CPU with MP/M II I/O port handling
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mpm_cpu.h"
#include "xios.h"
#include "banked_mem.h"
#include <iostream>
#include <iomanip>
#include <cstdlib>

MpmCpu::MpmCpu(qkz80_cpu_mem* memory)
    : qkz80(memory)
{
}

// Debug flag for post-boot port tracking
extern bool g_boot_display_complete;
bool g_trace_instructions = false;
int g_trace_count = 0;

void MpmCpu::port_out(qkz80_uint8 port, qkz80_uint8 value) {
    // Debug disabled for clean boot

    switch (port) {
        case MpmPorts::XIOS_DISPATCH:
            // XIOS dispatch: A register contains function offset
            // Protocol: LD A, function; OUT (0xE0), A; IN A, (0xE0); RET
            handle_xios_dispatch();
            break;

        case MpmPorts::BANK_SELECT:
            // Bank select: A register (value) contains bank number
            handle_bank_select(value);
            break;

        case MpmPorts::SIGNAL:
            // TEMPORARY DEBUG: OUT (0E2H),A with A!=0 traces A*500 instructions.
            std::cerr << "TRACEPORT hit value=" << (int)value << std::endl;
            if (value) { g_trace_instructions = true; g_trace_count = value * 500; }
            else { g_trace_instructions = false; }
            break;

        default:
            break;
    }
}

qkz80_uint8 MpmCpu::port_in(qkz80_uint8 port) {
    uint8_t value = 0xFF;

    switch (port) {
        case MpmPorts::XIOS_DISPATCH:
            // Return the result from the last XIOS dispatch
            // This is used with IN A, (0xE0) after OUT (0xE0), A to get return values
            value = last_xios_result_;
            break;

        case MpmPorts::SIGNAL:
            // Could return status here
            value = 0x00;
            break;

        default:
            break;
    }

    return value;
}

void MpmCpu::handle_xios_dispatch() {
    if (!xios_) {
        std::cerr << "[XIOS DISPATCH] Error: no XIOS handler set" << std::endl;
        return;
    }

    // Function offset is in A register (from OUT (port), A instruction)
    uint8_t func = regs.AF.get_high();

    // Dispatch to XIOS handler
    // The handler will set result registers (A, HL, etc.)
    // The Z80 code has its own RET instruction, so we don't simulate RET here
    xios_->handle_port_dispatch(func);

    // Save the result for IN A, (port) to read later
    // The XIOS handler sets the result in A register via set_high()
    last_xios_result_ = regs.AF.get_high();
}

void MpmCpu::handle_bank_select(uint8_t bank) {
    if (!banked_mem_) {
        std::cerr << "[BANK SELECT] Error: no banked memory set" << std::endl;
        return;
    }

    banked_mem_->select_bank(bank);

    // Notify XIOS of bank change for DMA targeting
    if (xios_) {
        xios_->update_dma_bank(bank);
    }
}

// halt() now inherited from qkz80 base class

void MpmCpu::execute(void) {
    // TEMPORARY DEBUG: arm the trace when a transient is entered.
    if (!g_trace_instructions && g_trace_count == 0) {
        static const char* at = getenv("MPM_TRACE_AT");
        if (at) {
            uint16_t want = (uint16_t)strtoul(at, nullptr, 16);
            static const char* bk = getenv("MPM_TRACE_BANK");
            int cur = banked_mem_ ? banked_mem_->current_bank() : 0;
            bool bank_ok = !bk || atoi(bk) == cur;
            if (regs.PC.get_pair16() == want && bank_ok) {
                const char* n = getenv("MPM_TRACE_N");
                g_trace_instructions = true;
                g_trace_count = n ? atoi(n) : 200;
            }
        }
    }
    if (g_trace_instructions && g_trace_count > 0) {
        uint16_t pc = regs.PC.get_pair16();
        std::cerr << "T " << std::hex << std::setfill('0') << std::setw(4) << pc
                  << " b" << (int)(banked_mem_ ? banked_mem_->current_bank() : 0)
                  << " sp" << std::setw(4) << regs.SP.get_pair16()
                  << " op" << std::setw(2) << (int)mem->fetch_mem(pc)
                  << " a" << std::setw(2) << (int)regs.AF.get_high()
                  << " bc" << std::setw(4) << regs.BC.get_pair16()
                  << " de" << std::setw(4) << regs.DE.get_pair16()
                  << " hl" << std::setw(4) << regs.HL.get_pair16()
                  << std::dec << "\n";
        if (--g_trace_count == 0) g_trace_instructions = false;
    }
    qkz80::execute();
}

void MpmCpu::unimplemented_opcode(qkz80_uint8 opcode, qkz80_uint16 pc) {
    // Encountered an invalid or unimplemented Z80 opcode
    std::cerr << "\n*** Unimplemented opcode 0x" << std::hex << (int)opcode
              << " at PC=0x" << pc << std::dec << " ***\n";
    std::cerr << "    Bank=" << (int)(banked_mem_ ? banked_mem_->current_bank() : 0)
              << " SP=0x" << std::hex << regs.SP.get_pair16()
              << " AF=0x" << regs.AF.get_pair16()
              << std::dec << std::endl;

    // Dump surrounding memory for context
    std::cerr << "    Memory at PC: ";
    for (int i = -2; i <= 5; i++) {
        uint16_t addr = pc + i;
        uint8_t byte = mem->fetch_mem(addr);
        if (i == 0) std::cerr << "[";
        std::cerr << std::hex << std::setfill('0') << std::setw(2) << (int)byte;
        if (i == 0) std::cerr << "]";
        std::cerr << " ";
    }
    std::cerr << std::dec << std::endl;

    set_halted();
}
