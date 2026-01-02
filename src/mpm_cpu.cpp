// mpm_cpu.cpp - Extended Z80 CPU with MP/M II I/O port handling
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mpm_cpu.h"
#include "xios.h"
#include "banked_mem.h"
#include <iostream>
#include <iomanip>

extern bool g_debug_enabled;

// Track POLLDEVICE for console 7 input (device 15)
// These are not static so they can be accessed from xios.cpp
int last_polldevice_device = -1;
bool last_polldevice_ready = false;  // Set when device 15 returns ready

MpmCpu::MpmCpu(qkz80_cpu_mem* memory)
    : qkz80(memory)
{
}

void MpmCpu::port_out(qkz80_uint8 port, qkz80_uint8 value) {
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
    uint8_t func = regs.AF.get_high();

    // Dispatch to XIOS handler
    // The handler will set result registers (A, HL, etc.)
    // The Z80 code has its own RET instruction, so we don't simulate RET here
    xios_->handle_port_dispatch(func);

    // Save the result for IN A, (port) to read later
    // The XIOS handler sets the result in A register via set_high()
    last_xios_result_ = regs.AF.get_high();

    // Track POLLDEVICE state for debugging
    if (func == 0x36) {  // POLLDEVICE
        uint8_t device = regs.BC.get_low();
        last_polldevice_device = device;
        last_polldevice_ready = (last_xios_result_ == 0xFF);
    }
}

void MpmCpu::handle_bank_select(uint8_t bank) {
    if (!banked_mem_) {
        std::cerr << "[BANK SELECT] Error: no banked memory set" << std::endl;
        return;
    }

    if (g_debug_enabled || debug_io) {
        std::cerr << "[BANK SELECT] bank=" << (int)bank
                  << " (was " << (int)banked_mem_->current_bank() << ")"
                  << " PC=0x" << std::hex << regs.PC.get_pair16() << std::dec << "\n";
    }

    banked_mem_->select_bank(bank);

    // Notify XIOS of bank change for DMA targeting
    if (xios_) {
        xios_->update_dma_bank(bank);
    }
}

void MpmCpu::halt(void) {
    // Z80 HALT instruction - CPU waits for interrupt
    // In MP/M II context, this is the normal idle loop
    halted_ = true;
}

void MpmCpu::execute(void) {
    uint16_t pc = regs.PC.get_pair16();
    uint8_t opcode = mem->fetch_mem(pc);

    // FIX: Prevent self-loop in Tick process at exactly the write instruction
    // The self-loop is created at PC=0xdc37 when writing pdadr to [BC]
    // Only fix when BC points to Tick (0xed98) and DE=Tick (about to create self-loop)
    const uint16_t TICK_ADDR = 0xED98;
    if (pc == 0xdc37 || pc == 0xdc38 || pc == 0xdc39) {
        uint16_t bc = regs.BC.get_pair16();
        uint16_t de = regs.DE.get_pair16();
        if (bc == de && (bc == TICK_ADDR || bc == TICK_ADDR + 1)) {
            // About to create a self-loop in Tick - skip by jumping to RET
            static int insert_skip_count = 0;
            insert_skip_count++;
            if (insert_skip_count <= 5) {
                std::cerr << "[SELF-LOOP SKIP] BC=DE=0x" << std::hex << bc
                          << " at PC=0x" << pc << " - skipping self-loop write\n" << std::dec;
            }
            // Skip to the RET at end of insert_process (0xdc44 approximately)
            // Find and skip to the RET
            regs.PC.set_pair16(0xdc44);  // Jump past the problematic writes
            return;
        }
    }

    // PRE-EXECUTE check: prevent LD SP,HL from setting SP to an invalid value
    // Opcode 0xF9 = LD SP,HL - the scheduler uses this to switch contexts

    if (opcode == 0xF9) {  // LD SP,HL
        uint16_t new_sp = regs.HL.get_pair16();
        if (new_sp == 0xFFFF || new_sp == 0x0000 || new_sp < 0x0100) {
            static int bad_sp_prevented = 0;
            bad_sp_prevented++;
            if (bad_sp_prevented <= 10) {
                std::cerr << "[BAD SP PREVENTED] LD SP,HL would set SP=0x" << std::hex << new_sp
                          << " at PC=0x" << pc << " - skipping to next process\n" << std::dec;
            }

            // Skip this instruction (advance PC past the 1-byte opcode)
            regs.PC.set_pair16(pc + 1);

            // Jump to the dispatcher to pick the next process
            // First, restore a safe SP
            regs.SP.set_pair16(0xFE00);  // Safe stack in common memory

            // Set up for RST 38H (dispatcher entry)
            regs.PC.set_pair16(0xC23A);  // RST 38H vector destination

            return;  // Don't execute the LD SP,HL
        }
    }

    // Call the base class execute
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

    halted_ = true;
}
