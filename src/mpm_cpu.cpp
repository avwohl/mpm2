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

// Debug flag for post-boot port tracking
extern bool g_boot_display_complete;
bool g_trace_instructions = false;
int g_trace_count = 0;

void MpmCpu::port_out(qkz80_uint8 port, qkz80_uint8 value) {
    // Debug disabled for clean boot

    switch (port) {
        case MpmPorts::XIOS_DISPATCH:
            // XIOS dispatch: A register contains function offset
            // Protocol: LD A, function; OUT (0xE0), A; RET - the result is in A
            handle_xios_dispatch();
            break;

        case MpmPorts::BANK_SELECT:
            // Bank select: A register (value) contains bank number
            handle_bank_select(value);
            break;

        case MpmPorts::SIGNAL:
            // Signal port - used for status
            break;

        default:
            break;
    }
}

qkz80_uint8 MpmCpu::port_in(qkz80_uint8 port) {
    uint8_t value = 0xFF;

    switch (port) {
        case MpmPorts::XIOS_DISPATCH:
            // The handler leaves an XIOS function's result in A as part of
            // the OUT (0E0H),A that dispatched it, and neither asm/bnkxios.asm
            // nor asm/sftp_glue.asm reads it again.  Code that still follows
            // the OUT with IN A,(0E0H) gets A itself back.  It must not
            // read a copy kept here: the 60Hz tick can land between the OUT
            // and the IN, the interrupt handler ends in the dispatcher, and
            // whichever process runs next makes XIOS calls of its own before
            // this one gets back to its IN.  A saved copy then held the other
            // process's result, and the BDOS took it for its own - a disk
            // read or write that had succeeded reported NONRECOVERABLE.  A is
            // saved and restored with the rest of the process's registers.
            value = regs.AF.get_high();
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
    // The handler leaves its result in A (and HL where there is one), which
    // is where the Z80 code takes it from after the OUT.
    xios_->handle_port_dispatch(func);
}

void MpmCpu::handle_bank_select(uint8_t bank) {
    if (!banked_mem_) {
        std::cerr << "[BANK SELECT] Error: no banked memory set" << std::endl;
        return;
    }

    banked_mem_->select_bank(bank);
}

// halt() now inherited from qkz80 base class

void MpmCpu::execute(void) {
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
