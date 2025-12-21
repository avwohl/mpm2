// mpm_cpu.cpp - Extended Z80 CPU with MP/M II I/O port handling
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mpm_cpu.h"
#include "xios.h"
#include "banked_mem.h"
#include <iostream>

MpmCpu::MpmCpu(qkz80_cpu_mem* memory)
    : qkz80(memory)
{
}

void MpmCpu::port_out(qkz80_uint8 port, qkz80_uint8 value) {
    switch (port) {
        case MpmPorts::XIOS_DISPATCH:
            // XIOS dispatch: B register contains function offset
            // Protocol: LD B, function; OUT (0xE0), A; RET
            handle_xios_dispatch();
            break;

        case MpmPorts::BANK_SELECT:
            // Bank select: A register (value) contains bank number
            handle_bank_select(value);
            break;

        case MpmPorts::SIGNAL:
            // Signal port - used for debug/status
            if (debug_io) {
                std::cerr << "[SIGNAL] value=0x" << std::hex << (int)value << std::dec << std::endl;
            }
            break;

        default:
            if (debug_io) {
                std::cerr << "[OUT] port=0x" << std::hex << (int)port
                          << " value=0x" << (int)value << std::dec << std::endl;
            }
            break;
    }
}

qkz80_uint8 MpmCpu::port_in(qkz80_uint8 port) {
    uint8_t value = 0xFF;

    switch (port) {
        case MpmPorts::SIGNAL:
            // Could return status here
            value = 0x00;
            break;

        default:
            if (debug_io) {
                std::cerr << "[IN] port=0x" << std::hex << (int)port << std::dec << std::endl;
            }
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
    // For functions that don't use BC as a 16-bit value, B may also have the function.
    // We prefer A since that's what OUT actually outputs.
    uint8_t func = regs.AF.get_high();  // A register

    // Debug trace - always trace SETTRK to catch corruption
    static int trace_count = 0;
    bool is_settrk = (func == 0x1e);
    if (trace_count++ < 50 || is_settrk) {
        std::cerr << "[XIOS DISPATCH] func=0x" << std::hex << (int)func
                  << " BC=0x" << regs.BC.get_pair16()
                  << " DE=0x" << regs.DE.get_pair16()
                  << " HL=0x" << regs.HL.get_pair16();
        if (is_settrk && regs.HL.get_pair16() > 100) {
            std::cerr << " **TRACK CORRUPTION!**";
        }
        std::cerr << std::dec << std::endl;
    }

    // Dispatch to XIOS handler
    // The handler will set result registers (A, HL, etc.)
    // The Z80 code has its own RET instruction, so we don't simulate RET here
    xios_->handle_port_dispatch(func);
}

void MpmCpu::handle_bank_select(uint8_t bank) {
    if (!banked_mem_) {
        std::cerr << "[BANK SELECT] Error: no banked memory set" << std::endl;
        return;
    }

    if (debug_io) {
        std::cerr << "[BANK SELECT] bank=" << (int)bank << std::endl;
    }

    banked_mem_->select_bank(bank);
}
