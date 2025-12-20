// xios.cpp - MP/M II Extended I/O System implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "xios.h"
#include "console.h"
#include "banked_mem.h"
#include "disk.h"
#include "qkz80.h"
#include <iostream>

XIOS::XIOS(qkz80* cpu, BankedMemory* mem)
    : cpu_(cpu)
    , mem_(mem)
    , xios_base_(0xFC00)   // Default, will be set by GENSYS
    , ldrbios_base_(0x1700) // LDRBIOS for boot phase
    , bdos_stub_(0x0D06)    // MPMLDR's internal BDOS entry
    , current_disk_(0)
    , current_track_(0)
    , current_sector_(0)
    , dma_addr_(0x0080)
    , tick_enabled_(false)
    , preempted_(false)
{
}

bool XIOS::is_xios_call(uint16_t pc) const {
    // Check XIOS range (0xFC00-0xFC48)
    if (pc >= xios_base_ && pc < xios_base_ + 0x100) {
        uint16_t offset = pc - xios_base_;
        // Valid entry point (multiples of 3 up to IDLE)
        return (offset <= XIOS_IDLE) && (offset % 3 == 0);
    }

    // Check LDRBIOS range (0xF000-0xF030)
    if (pc >= ldrbios_base_ && pc < ldrbios_base_ + 0x100) {
        uint16_t offset = pc - ldrbios_base_;
        // LDRBIOS only has standard entries up to SECTRAN (0x30)
        return (offset <= XIOS_SECTRAN) && (offset % 3 == 0);
    }

    // Note: MPMLDR has its own internal BDOS at 0x0D06
    // We don't intercept it - it will call LDRBIOS which we do intercept

    return false;
}

bool XIOS::handle_call(uint16_t pc) {
    if (!is_xios_call(pc)) return false;

    // Compute offset - works for both XIOS and LDRBIOS
    uint16_t offset;
    bool is_ldrbios = (pc < xios_base_);
    if (pc >= xios_base_) {
        offset = pc - xios_base_;
    } else {
        offset = pc - ldrbios_base_;
    }

    // For LDRBIOS, let SELDSK run natively to return correct DPH pointer
    // The LDRBIOS has its own DPH/DPB tables that we must use
    if (is_ldrbios && offset == XIOS_SELDSK) {
        return false;  // Don't intercept - let Z80 code run
    }

    switch (offset) {
        case XIOS_BOOT:      do_boot(); break;
        case XIOS_WBOOT:     do_wboot(); break;
        case XIOS_CONST:     do_const(); break;
        case XIOS_CONIN:     do_conin(); break;
        case XIOS_CONOUT:    do_conout(); break;
        case XIOS_LIST:      do_list(); break;
        case XIOS_PUNCH:     do_punch(); break;
        case XIOS_READER:    do_reader(); break;
        case XIOS_HOME:      do_home(); break;
        case XIOS_SELDSK:    do_seldsk(); break;
        case XIOS_SETTRK:    do_settrk(); break;
        case XIOS_SETSEC:    do_setsec(); break;
        case XIOS_SETDMA:    do_setdma(); break;
        case XIOS_READ:      do_read(); break;
        case XIOS_WRITE:     do_write(); break;
        case XIOS_LISTST:    do_listst(); break;
        case XIOS_SECTRAN:   do_sectran(); break;
        case XIOS_SELMEMORY: do_selmemory(); break;
        case XIOS_POLLDEVICE: do_polldevice(); break;
        case XIOS_STARTCLOCK: do_startclock(); break;
        case XIOS_STOPCLOCK:  do_stopclock(); break;
        case XIOS_EXITREGION: do_exitregion(); break;
        case XIOS_MAXCONSOLE: do_maxconsole(); break;
        case XIOS_SYSTEMINIT: do_systeminit(); break;
        case XIOS_IDLE:       do_idle(); break;
        default:
            return false;  // Unknown entry
    }

    return true;
}

void XIOS::do_ret() {
    // Pop return address from stack and set PC
    uint16_t sp = cpu_->regs.SP.get_pair16();
    uint8_t lo = mem_->fetch_mem(sp);
    uint8_t hi = mem_->fetch_mem(sp + 1);
    cpu_->regs.SP.set_pair16(sp + 2);
    cpu_->regs.PC.set_pair16((hi << 8) | lo);
}

// Console I/O - D register contains console number
void XIOS::do_const() {
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    Console* con = ConsoleManager::instance().get(console);

    if (con) {
        cpu_->regs.AF.set_high(con->const_status());
    } else {
        cpu_->regs.AF.set_high(0x00);
    }
    do_ret();
}

void XIOS::do_conin() {
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    Console* con = ConsoleManager::instance().get(console);

    if (con) {
        cpu_->regs.AF.set_high(con->read_char());
    } else {
        cpu_->regs.AF.set_high(0x1A);  // EOF
    }
    do_ret();
}

void XIOS::do_conout() {
    // For LDRBIOS, console 0 is always used
    // For XIOS, D = console number
    uint8_t console = 0;  // Default to console 0 for boot
    if (cpu_->regs.PC.get_pair16() >= xios_base_) {
        console = cpu_->regs.DE.get_high();  // D = console number for XIOS
    }
    uint8_t ch = cpu_->regs.BC.get_low();        // C = character
    Console* con = ConsoleManager::instance().get(console);

    if (con) {
        con->write_char(ch);
    }
    do_ret();
}

void XIOS::do_list() {
    // List device (printer) - not implemented yet
    do_ret();
}

void XIOS::do_punch() {
    // Punch device - not implemented
    do_ret();
}

void XIOS::do_reader() {
    // Reader device - return EOF
    cpu_->regs.AF.set_high(0x1A);
    do_ret();
}

void XIOS::do_listst() {
    // List status - always ready
    cpu_->regs.AF.set_high(0xFF);
    do_ret();
}

// Disk I/O - placeholder implementations
void XIOS::do_home() {
    current_track_ = 0;
    do_ret();
}

void XIOS::do_seldsk() {
    uint8_t disk = cpu_->regs.BC.get_low();  // C = disk number

    // Check if disk is valid (mounted)
    if (!DiskSystem::instance().select(disk)) {
        cpu_->regs.HL.set_pair16(0x0000);  // Error - no such disk
        do_ret();
        return;
    }

    current_disk_ = disk;

    // Return DPH address
    // DPH table is at XIOS_BASE + 0x100 (0xFD00 by default)
    // Each DPH is 16 bytes
    uint16_t dph_addr = xios_base_ + 0x100 + (disk * 16);
    cpu_->regs.HL.set_pair16(dph_addr);
    do_ret();
}

void XIOS::do_settrk() {
    current_track_ = cpu_->regs.BC.get_pair16();  // BC = track number
    do_ret();
}

void XIOS::do_setsec() {
    current_sector_ = cpu_->regs.BC.get_pair16();  // BC = sector number
    do_ret();
}

void XIOS::do_setdma() {
    dma_addr_ = cpu_->regs.BC.get_pair16();  // BC = DMA address
    do_ret();
}

void XIOS::do_read() {
    // Set up disk system with current parameters
    DiskSystem::instance().set_track(current_track_);
    DiskSystem::instance().set_sector(current_sector_);
    DiskSystem::instance().set_dma(dma_addr_);

    // Perform read
    int result = DiskSystem::instance().read(mem_);
    cpu_->regs.AF.set_high(result);
    do_ret();
}

void XIOS::do_write() {
    // Set up disk system with current parameters
    DiskSystem::instance().set_track(current_track_);
    DiskSystem::instance().set_sector(current_sector_);
    DiskSystem::instance().set_dma(dma_addr_);

    // Perform write
    int result = DiskSystem::instance().write(mem_);
    cpu_->regs.AF.set_high(result);
    do_ret();
}

void XIOS::do_sectran() {
    // Sector translation - return BC unchanged for now
    cpu_->regs.HL.set_pair16(cpu_->regs.BC.get_pair16());
    do_ret();
}

// Extended XIOS entries

void XIOS::do_selmemory() {
    // BC = address of memory descriptor
    // descriptor: base(1), size(1), attrib(1), bank(1)
    uint16_t desc_addr = cpu_->regs.BC.get_pair16();
    uint8_t bank = mem_->fetch_mem(desc_addr + 3);  // Get bank byte

    mem_->select_bank(bank);
    do_ret();
}

void XIOS::do_polldevice() {
    // C = device number to poll
    // Return 0xFF if ready, 0x00 if not
    uint8_t device = cpu_->regs.BC.get_low();

    // Device 0 = printer (always ready for now)
    // Device 1-4 = console output 0-3
    // Device 5-8 = console input 0-3

    uint8_t result = 0x00;

    if (device == 0) {
        // Printer - always ready
        result = 0xFF;
    } else if (device >= 1 && device <= 4) {
        // Console output - always ready
        result = 0xFF;
    } else if (device >= 5 && device <= 8) {
        // Console input
        int console = device - 5;
        Console* con = ConsoleManager::instance().get(console);
        if (con && con->const_status()) {
            result = 0xFF;
        }
    }

    cpu_->regs.AF.set_high(result);
    do_ret();
}

void XIOS::do_startclock() {
    tick_enabled_.store(true);
    do_ret();
}

void XIOS::do_stopclock() {
    tick_enabled_.store(false);
    do_ret();
}

void XIOS::do_exitregion() {
    // Enable interrupts if not preempted
    if (!preempted_.load()) {
        cpu_->regs.IFF1 = 1;
        cpu_->regs.IFF2 = 1;
    }
    do_ret();
}

void XIOS::do_maxconsole() {
    cpu_->regs.AF.set_high(MAX_CONSOLES);
    do_ret();
}

void XIOS::do_systeminit() {
    // C = breakpoint RST number
    // DE = breakpoint handler address
    // HL = XIOS direct jump table address

    // TODO: Set up interrupt vectors in each bank
    // For now, just initialize consoles
    ConsoleManager::instance().init();

    do_ret();
}

void XIOS::do_idle() {
    // Called when no processes are ready
    // For a polled system, this would call the dispatcher
    // For us, we can just return (or yield briefly)
    do_ret();
}

void XIOS::do_boot() {
    // Cold boot - restart the system
    // TODO: Implement proper boot sequence
    do_ret();
}

void XIOS::do_wboot() {
    // Warm boot - terminate current process
    // In MP/M, this goes back to TMP
    do_ret();
}

void XIOS::tick() {
    // Called from timer interrupt (60Hz)
    // Set flag #1 if clock is enabled
    if (tick_enabled_.load()) {
        // TODO: Set MP/M flag #1
    }
}

void XIOS::one_second_tick() {
    // Called once per second
    // TODO: Set MP/M flag #2
}

void XIOS::do_bdos() {
    // Minimal BDOS for boot phase (MPMLDR)
    // C = function number, DE = parameter
    uint8_t func = cpu_->regs.BC.get_low();
    uint16_t de = cpu_->regs.DE.get_pair16();

    // Debug output
    static int call_count = 0;
    if (call_count < 50) {
        std::cerr << "[BDOS] func=" << (int)func << " DE=0x" << std::hex << de << std::dec << "\n";
        call_count++;
    }

    switch (func) {
        case 0:  // System reset
            // Return to CCP - for loader, just return
            break;

        case 1:  // Console input
            // Read character with echo
            {
                Console* con = ConsoleManager::instance().get(0);
                if (con) {
                    uint8_t ch = con->read_char();
                    cpu_->regs.AF.set_high(ch);
                    con->write_char(ch);  // Echo
                } else {
                    cpu_->regs.AF.set_high(0x1A);
                }
            }
            break;

        case 2:  // Console output
            // Output character in E
            {
                Console* con = ConsoleManager::instance().get(0);
                if (con) {
                    con->write_char(de & 0xFF);
                }
            }
            break;

        case 6:  // Direct console I/O
            if ((de & 0xFF) == 0xFF) {
                // Input
                Console* con = ConsoleManager::instance().get(0);
                if (con && con->const_status()) {
                    cpu_->regs.AF.set_high(con->read_char());
                } else {
                    cpu_->regs.AF.set_high(0);
                }
            } else {
                // Output
                Console* con = ConsoleManager::instance().get(0);
                if (con) {
                    con->write_char(de & 0xFF);
                }
            }
            break;

        case 9:  // Print string (terminated by $)
            {
                Console* con = ConsoleManager::instance().get(0);
                if (con) {
                    uint16_t addr = de;
                    for (int i = 0; i < 1000; i++) {  // Safety limit
                        uint8_t ch = mem_->fetch_mem(addr++);
                        if (ch == '$') break;
                        con->write_char(ch);
                    }
                }
            }
            break;

        case 11: // Console status
            {
                Console* con = ConsoleManager::instance().get(0);
                cpu_->regs.AF.set_high(con && con->const_status() ? 0xFF : 0x00);
            }
            break;

        case 12: // Return version number
            // MP/M II returns 0x21 (version 2.1) with bit 7 set for MP/M
            cpu_->regs.HL.set_pair16(0x0021);
            cpu_->regs.AF.set_high(0x21);
            break;

        case 13: // Reset disk system
            DiskSystem::instance().select(0);
            current_disk_ = 0;
            break;

        case 14: // Select disk
            current_disk_ = de & 0x0F;
            DiskSystem::instance().select(current_disk_);
            cpu_->regs.AF.set_high(0);  // Success
            break;

        case 15: // Open file
            // TODO: Implement file operations for MPMLDR
            cpu_->regs.AF.set_high(0xFF);  // Not found for now
            break;

        case 20: // Read sequential
            // TODO: Implement for MPMLDR to read MPM.SYS
            cpu_->regs.AF.set_high(1);  // EOF for now
            break;

        case 26: // Set DMA address
            dma_addr_ = de;
            break;

        default:
            // Unknown function - just return
            break;
    }

    do_ret();
}
