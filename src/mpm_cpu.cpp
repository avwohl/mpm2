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
bool last_polldevice_ready = false;

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
            // Debug: log when returning 0xFF from POLLDEVICE
            if (value == 0xFF) {
                uint16_t pc = regs.PC.get_pair16();
                // Check if this is after a POLLDEVICE call (PC around 0xC1E8)
                if (pc >= 0xC1E0 && pc <= 0xC1F0) {
                    static int polldev_in_ff_count = 0;
                    polldev_in_ff_count++;
                    // Suppress the per-POLLDEVICE trace to reduce noise
                    // Tracing is now enabled by xios.cpp after all 5 devices return READY
                }
            }
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

    // Debug: trace when last_xios_result_ is set to 0xFF at CHKCON (PC=0xC262)
    if (g_debug_enabled && (regs.PC.get_pair16() == 0xC262 || regs.PC.get_pair16() == 0xC263) && last_xios_result_ == 0xFF) {
        static int set_result_ff_count = 0;
        set_result_ff_count++;
        if (set_result_ff_count <= 20) {
            std::cerr << "[SET RESULT FF] PC=0x" << std::hex << regs.PC.get_pair16()
                      << " func=0x" << (int)func
                      << " A=" << (int)regs.AF.get_high()
                      << " last_xios_result=0x" << (int)last_xios_result_
                      << " #" << std::dec << set_result_ff_count << "\n";
        }
    }

    // Debug: trace POLLDEVICE result for input devices that return READY
    if (g_debug_enabled && func == 0x36) {  // POLLDEVICE
        uint8_t device = regs.BC.get_low();
        if ((device & 1) && last_xios_result_ == 0xFF) {  // Input device ready
            static int poll_ready_count = 0;
            poll_ready_count++;
            if (poll_ready_count <= 20) {
                std::cerr << "[POLLDEV] device=" << (int)device
                          << " console=" << (device / 2)
                          << " result=0x" << std::hex << (int)last_xios_result_
                          << " PC=0x" << regs.PC.get_pair16() << std::dec
                          << " #" << poll_ready_count << "\n";
            }
        }
    }

    // Debug trace for POLLDEVICE calls that return READY
    if (g_debug_enabled && func == 0x36 && last_xios_result_ == 0xFF) {  // POLLDEVICE
        static int polldev_ready_count = 0;
        polldev_ready_count++;
        if (polldev_ready_count <= 30) {
            std::cerr << "[XIOS DISPATCH] POLLDEVICE returning READY, dev="
                      << (int)regs.BC.get_low() << " PC=0x" << std::hex
                      << regs.PC.get_pair16() << " E=" << (int)regs.DE.get_low()
                      << std::dec << " #" << polldev_ready_count << "\n";
        }
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
