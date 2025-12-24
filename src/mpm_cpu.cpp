// mpm_cpu.cpp - Extended Z80 CPU with MP/M II I/O port handling
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mpm_cpu.h"
#include "xios.h"
#include "banked_mem.h"
#include <iostream>
#include <iomanip>

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

    // Debug trace - trace dispatches with unknown functions
    if (func != 0x00 && func != 0x03 && func != 0x06 && func != 0x09 &&
        func != 0x0C && func != 0x0F && func != 0x12 && func != 0x15 &&
        func != 0x18 && func != 0x1B && func != 0x1E && func != 0x21 &&
        func != 0x24 && func != 0x27 && func != 0x2A && func != 0x2D &&
        func != 0x30 && func != 0x33 && func != 0x36 && func != 0x39 &&
        func != 0x3C && func != 0x3F && func != 0x42 && func != 0x45 &&
        func != 0x48 && func != 0x4B && func != 0x4E && func != 0x51 &&
        func != 0x54 && func != 0x57) {
        std::cerr << "[XIOS DISPATCH] Unknown func=0x" << std::hex << (int)func
                  << " BC=0x" << regs.BC.get_pair16()
                  << " DE=0x" << regs.DE.get_pair16()
                  << " HL=0x" << regs.HL.get_pair16()
                  << " PC=0x" << regs.PC.get_pair16()
                  << std::dec << std::endl;
        // Dump FC00-FC30 to see if xios_port code is still there
        std::cerr << "FC00: ";
        for (int i = 0; i < 16; i++) std::cerr << std::hex << (int)mem->fetch_mem(0xFC00 + i) << " ";
        std::cerr << std::endl;
        std::cerr << "FC7F: ";
        for (int i = 0; i < 16; i++) std::cerr << std::hex << (int)mem->fetch_mem(0xFC7F + i) << " ";
        std::cerr << std::endl;
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

void MpmCpu::halt(void) {
    // Z80 HALT instruction - CPU waits for interrupt
    // In MP/M II context, this typically means system idle or error
    std::cerr << "\n*** HALT instruction at PC=0x" << std::hex
              << regs.PC.get_pair16() << std::dec << " ***\n";
    std::cerr << "    Bank=" << (int)(banked_mem_ ? banked_mem_->current_bank() : 0)
              << " SP=0x" << std::hex << regs.SP.get_pair16()
              << " AF=0x" << regs.AF.get_pair16()
              << " BC=0x" << regs.BC.get_pair16()
              << " DE=0x" << regs.DE.get_pair16()
              << " HL=0x" << regs.HL.get_pair16()
              << std::dec << std::endl;
    halted_ = true;
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

    halted_ = true;
}
