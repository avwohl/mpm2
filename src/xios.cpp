// xios.cpp - MP/M II Extended I/O System implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "xios.h"
#include "console.h"
#include "banked_mem.h"
#include "disk.h"
#include "qkz80.h"
#include <iostream>
#include <set>

XIOS::XIOS(qkz80* cpu, BankedMemory* mem)
    : cpu_(cpu)
    , mem_(mem)
    , xios_base_(0x8800)   // Default - below TMP (9100H) but in common memory
    , current_disk_(0)
    , current_track_(0)
    , current_sector_(0)
    , dma_addr_(0x0080)
    , dma_bank_(0)
    , tick_enabled_(false)
    , preempted_(false)
{
}

void XIOS::handle_port_dispatch(uint8_t func) {
    skip_ret_ = true;

    switch (func) {
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
        case XIOS_COMMONBASE: do_boot(); break;  // Returns commonbase address
        case XIOS_SWTUSER:    do_swtuser(); break;
        case XIOS_SWTSYS:     do_swtsys(); break;
        case XIOS_PDISP:      do_pdisp(); break;
        case XIOS_XDOSENT:    do_xdosent(); break;
        case XIOS_SYSDAT:     do_sysdat(); break;

        default:
            // Only warn once per unknown function to avoid spamming
            static std::set<uint8_t> warned_funcs;
            if (warned_funcs.find(func) == warned_funcs.end()) {
                warned_funcs.insert(func);
                std::cerr << "[XIOS PORT] Unknown function 0x" << std::hex << (int)func << std::dec
                          << " (further occurrences suppressed)\n";
            }
            break;
    }

    skip_ret_ = false;
}

void XIOS::do_ret() {
    // Skip RET when using I/O port dispatch (Z80 code has its own RET)
    if (skip_ret_) return;
    // Pop return address from stack and set PC
    uint16_t sp = cpu_->regs.SP.get_pair16();
    uint8_t lo = mem_->fetch_mem(sp);
    uint8_t hi = mem_->fetch_mem(sp + 1);
    uint16_t ret_addr = (hi << 8) | lo;
    cpu_->regs.SP.set_pair16(sp + 2);
    cpu_->regs.PC.set_pair16(ret_addr);
}

// Console I/O - D register contains console number (MP/M II XIOS convention)
// If D is invalid (>=8), default to console 0 (workaround for possible XDOS issue)
void XIOS::do_const() {
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    if (console >= 8) console = 0;  // Workaround: invalid console -> default to 0

    Console* con = ConsoleManager::instance().get(console);
    uint8_t status = con ? con->const_status() : 0x00;
    cpu_->regs.AF.set_high(status);
    do_ret();
}

void XIOS::do_conin() {
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    if (console >= 8) console = 0;  // Workaround: invalid console -> default to 0

    Console* con = ConsoleManager::instance().get(console);
    uint8_t ch = con ? con->read_char() : 0x1A;  // EOF default if no console

    cpu_->regs.AF.set_high(ch);
    do_ret();
}

void XIOS::do_conout() {
    // D = console number (MP/M II XIOS convention), C = character
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    if (console >= 8) console = 0;  // Workaround: invalid console -> default to 0
    uint8_t ch = cpu_->regs.BC.get_low();        // C = character

    static int conout_count = 0;
    if (++conout_count <= 50) {
        std::cerr << "[CONOUT] con=" << (int)console << " ch=0x" << std::hex << (int)ch << std::dec;
        if (ch >= 0x20 && ch < 0x7f) std::cerr << " '" << (char)ch << "'";
        std::cerr << "\n";
    }

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
    // Select disk - validates disk and returns success/error in A
    // Z80 code calculates DPH address from its own DPH_TABLE
    uint8_t disk = cpu_->regs.BC.get_low();  // C = disk number

    // Check if disk is valid (mounted and within range)
    // DiskSystem::select() returns false if disk is out of range or unmounted
    if (!DiskSystem::instance().select(disk)) {
        cpu_->regs.AF.set_high(0xFF);  // Return error
        do_ret();
        return;
    }

    current_disk_ = disk;
    cpu_->regs.AF.set_high(0);  // Return success
    // Note: Z80 code sets HL to DPH address after this returns
    do_ret();
}

void XIOS::do_settrk() {
    // For port dispatch: assembly copies BC to HL before OUT
    // For PC-based dispatch (legacy): BC = track number
    current_track_ = skip_ret_ ? cpu_->regs.HL.get_pair16() : cpu_->regs.BC.get_pair16();
    do_ret();
}

void XIOS::do_setsec() {
    // For port dispatch: assembly copies BC to HL before OUT
    // For PC-based dispatch (legacy): BC = sector number
    current_sector_ = skip_ret_ ? cpu_->regs.HL.get_pair16() : cpu_->regs.BC.get_pair16();
    do_ret();
}

void XIOS::do_setdma() {
    // For port dispatch: assembly copies BC to HL before OUT
    // For PC-based dispatch (legacy): BC = DMA address
    dma_addr_ = skip_ret_ ? cpu_->regs.HL.get_pair16() : cpu_->regs.BC.get_pair16();
    do_ret();
}

void XIOS::do_read() {
    DiskSystem::instance().set_track(current_track_);
    DiskSystem::instance().set_sector(current_sector_);
    DiskSystem::instance().set_dma(dma_addr_, dma_bank_);

    int result = DiskSystem::instance().read(mem_);
    cpu_->regs.AF.set_high(result);
    do_ret();
}

void XIOS::do_write() {
    DiskSystem::instance().set_track(current_track_);
    DiskSystem::instance().set_sector(current_sector_);
    DiskSystem::instance().set_dma(dma_addr_, dma_bank_);

    int result = DiskSystem::instance().write(mem_);
    cpu_->regs.AF.set_high(result);
    do_ret();
}

void XIOS::do_sectran() {
    // Sector translation
    // For port dispatch: assembly copies BC to HL before OUT, DE = translation table
    // For PC-based dispatch (legacy): BC = logical sector, DE = translation table
    uint16_t logical = skip_ret_ ? cpu_->regs.HL.get_pair16() : cpu_->regs.BC.get_pair16();
    uint16_t xlat_table = cpu_->regs.DE.get_pair16();

    uint16_t physical = logical;  // Default: no translation

    // If translation table is provided, use it
    if (xlat_table != 0) {
        // Table contains physical sector numbers for each logical sector
        physical = mem_->fetch_mem(xlat_table + logical);
    }

    cpu_->regs.HL.set_pair16(physical);
    do_ret();
}

// Extended XIOS entries

void XIOS::do_selmemory() {
    // BC = address of memory descriptor
    // descriptor: base(1), size(1), attrib(1), bank(1)
    uint16_t desc_addr = cpu_->regs.BC.get_pair16();
    uint8_t bank = mem_->fetch_mem(desc_addr + 3);

    // Track last non-zero bank as the DMA target for user data
    if (bank != 0) {
        dma_bank_ = bank;
    }

    mem_->select_bank(bank);
    do_ret();
}

void XIOS::do_polldevice() {
    // C = device number to poll
    // Return 0xFF if ready, 0x00 if not
    // Device numbering:
    //   Even devices (0,2,4,...,14) = console output 0-7
    //   Odd devices (1,3,5,...,15) = console input 0-7
    //   Console number = device / 2
    uint8_t device = cpu_->regs.BC.get_low();
    uint8_t result = 0x00;

    if (device < 16) {
        int console = device / 2;
        bool is_input = (device & 1) != 0;

        if (console < 8) {
            if (is_input) {
                Console* con = ConsoleManager::instance().get(console);
                if (con && con->const_status()) {
                    result = 0xFF;
                }
            } else {
                // Console output - always ready
                result = 0xFF;
            }
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
    // Exit mutual exclusion region
    // NOTE: Do NOT enable interrupts here - the Z80 EXITRGN code handles it
    // It checks PREEMP flag and only does EI if not preempted
    do_ret();
}

void XIOS::do_maxconsole() {
    uint8_t num_consoles = mem_->read_common(0xFF01);
    cpu_->regs.AF.set_high(num_consoles);
    do_ret();
}

void XIOS::do_systeminit() {
    // C = breakpoint RST number
    // DE = breakpoint handler address
    // HL = XIOS direct jump table address
    //
    // The real XIOS SYSINIT only sets up RST 38H interrupt vector and initializes
    // hardware. It does NOT patch other banks - that's done by MP/M's process
    // initialization code. See RESXIOS.ASM lines 583-585.

    // Initialize consoles
    ConsoleManager::instance().init();

    // Enable timer interrupts
    tick_enabled_.store(true);
    do_ret();
}

void XIOS::do_idle() {
    do_ret();
}

// Commonbase entries - called by XDOS/BNKBDOS for bank switching and dispatch

void XIOS::do_swtuser() {
    uint16_t desc_addr = cpu_->regs.BC.get_pair16();
    if (desc_addr != 0) {
        uint8_t bank = mem_->fetch_mem(desc_addr + 3);
        mem_->select_bank(bank);
    }
    do_ret();
}

void XIOS::do_swtsys() {
    mem_->select_bank(0);
    do_ret();
}

void XIOS::do_pdisp() {
    // Process dispatcher entry point - called at end of interrupt handler
    // The Z80 code does EI then JP PDISP, but EI's effect is delayed
    // Re-enable interrupts here to ensure they stay enabled
    cpu_->regs.IFF1 = 1;
    cpu_->regs.IFF2 = 1;
    do_ret();
}

void XIOS::do_xdosent() {
    // XDOS entry point
    do_ret();
}

void XIOS::do_sysdat() {
    // Return pointer to system data area (SYSDAT) in HL
    // SYSDAT is at FF00H
    cpu_->regs.HL.set_pair16(0xFF00);
    do_ret();
}

void XIOS::do_boot() {
    // For MP/M II, COLDBOOT (offset 0) returns HL = address of commonbase
    // The commonbase structure is in XIOSJMP (at FC00H)
    uint16_t xiosjmp_addr = 0xFC00;

    // Commonbase structure starts at offset 0x4B in XIOSJMP
    uint16_t commonbase = xiosjmp_addr + XIOS_COMMONBASE;  // FC00+4B = FC4B

    cpu_->regs.HL.set_pair16(commonbase);
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
    // (The Z80 interrupt handler handles the actual flag setting via XDOS)
}

void XIOS::one_second_tick() {
    // Called once per second - could set MP/M flag #2 if needed
}
