// xios.cpp - MP/M II Extended I/O System implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "xios.h"
#include "console.h"
#include "banked_mem.h"
#include "disk.h"
#include "qkz80.h"
#include <iostream>
#include <iomanip>
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
{
}

void XIOS::handle_port_dispatch(uint8_t func) {

    switch (func) {
        case XIOS_BOOT:      do_boot(); break;
        case XIOS_WBOOT:     do_wboot(); break;
        case XIOS_CONST:     do_const(); break;
        case XIOS_CONIN:     do_conin(); break;
        case XIOS_CONOUT:    do_conout(); break;
        case XIOS_HOME:      do_home(); break;
        case XIOS_SELDSK:    do_seldsk(); break;
        case XIOS_SETTRK:    do_settrk(); break;
        case XIOS_SETSEC:    do_setsec(); break;
        case XIOS_SETDMA:    do_setdma(); break;
        case XIOS_READ:      do_read(); break;
        case XIOS_WRITE:     do_write(); break;
        case XIOS_SECTRAN:   do_sectran(); break;
        case XIOS_SELMEMORY: do_selmemory(); break;
        case XIOS_POLLDEVICE: do_polldevice(); break;
        case XIOS_STARTCLOCK: do_startclock(); break;
        case XIOS_STOPCLOCK:  do_stopclock(); break;
//        case XIOS_EXITREGION: do_exitregion(); break;
        case XIOS_MAXCONSOLE: do_maxconsole(); break;
        case XIOS_SYSTEMINIT: do_systeminit(); break;
        case XIOS_IDLE:       do_idle(); break;

        default:
            // Log unknown functions with PC for debugging
            {
                static std::set<uint8_t> warned_funcs;
                if (warned_funcs.find(func) == warned_funcs.end()) {
                    warned_funcs.insert(func);
                    uint16_t pc = cpu_->regs.PC.get_pair16();
                    std::cerr << "[XIOS PORT] Unknown function 0x" << std::hex << (int)func
                              << " at PC=0x" << pc << std::dec
                              << " (further occurrences suppressed)\n";
		    exit(1);
                }
            }
            break;
    }
}

// Console I/O - D register contains console number (MP/M II XIOS convention)
// If D is invalid (>=8), default to console 0 (workaround for possible XDOS issue)
void XIOS::do_const() {
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    if (console >= 8)
      throw std::invalid_argument("invalid console status"); 

    Console* con = ConsoleManager::instance().get(console);
    uint8_t status = con ? con->const_status() : 0x00;

    cpu_->regs.AF.set_high(status);
}

void XIOS::do_conin() {
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    if (console >= 8)
      throw std::invalid_argument("invalid console conin"); 

    Console* con = ConsoleManager::instance().get(console);
    uint8_t ch = con ? con->read_char() : 0x1A;  // EOF default if no console

    cpu_->regs.AF.set_high(ch);
}

void XIOS::do_conout() {
    // D = console number (MP/M II XIOS convention), C = character
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    if (console >= 8)
      throw std::invalid_argument("invalid console conout"); 
    uint8_t ch = cpu_->regs.BC.get_low();        // C = character

    Console* con = ConsoleManager::instance().get(console);
    if (con) {
        con->write_char(ch);
    }
}

// Disk I/O
void XIOS::do_home() {
    current_track_ = 0;
}

void XIOS::do_seldsk() {
    // Select disk - validates disk and returns success/error in A
    // Z80 code calculates DPH address from its own DPH_TABLE
    uint8_t disk = cpu_->regs.BC.get_low();  // C = disk number

    // Check if disk is valid (mounted and within range)
    // DiskSystem::select() returns false if disk is out of range or unmounted
    if (!DiskSystem::instance().select(disk)) {
        cpu_->regs.AF.set_high(0xFF);  // Return error
        return;
    }

    current_disk_ = disk;
    cpu_->regs.AF.set_high(0);  // Return success
}

void XIOS::do_settrk() {
    // Assembly copies BC to HL before OUT
    current_track_ = cpu_->regs.HL.get_pair16();
}

void XIOS::do_setsec() {
    // Assembly copies BC to HL before OUT
    current_sector_ = cpu_->regs.HL.get_pair16();
}

void XIOS::do_setdma() {
    // Assembly copies BC to HL before OUT
    dma_addr_ = cpu_->regs.HL.get_pair16();
}

void XIOS::do_read() {
    DiskSystem::instance().set_track(current_track_);
    DiskSystem::instance().set_sector(current_sector_);
    DiskSystem::instance().set_dma(dma_addr_, dma_bank_);

    int result = DiskSystem::instance().read(mem_);
    cpu_->regs.AF.set_high(result);
}

void XIOS::do_write() {
    DiskSystem::instance().set_track(current_track_);
    DiskSystem::instance().set_sector(current_sector_);
    DiskSystem::instance().set_dma(dma_addr_, dma_bank_);

    int result = DiskSystem::instance().write(mem_);
    cpu_->regs.AF.set_high(result);
}

void XIOS::do_sectran() {
    // Assembly copies BC to HL before OUT, DE = translation table
    uint16_t logical = cpu_->regs.HL.get_pair16();
    uint16_t xlat_table = cpu_->regs.DE.get_pair16();

    uint16_t physical = logical;  // Default: no translation

    // If translation table is provided, use it
    if (xlat_table != 0) {
        physical = mem_->fetch_mem(xlat_table + logical);
    }

    cpu_->regs.HL.set_pair16(physical);
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
    if (device > 15)
      throw std::invalid_argument("invalid device"); 

    int console = device / 2;
    bool is_input = (device & 1) != 0;

    if (console > 7)
      throw std::invalid_argument("invalid console polldevice"); 

    Console* con = ConsoleManager::instance().get(console);
    if (is_input) {
      uint8_t status = con ? con->const_status() : 0;
      if (status) {
	result = 0xFF;
      }
    } else {
      // Console output - ready if queue has space OR console not connected
      // For unconnected consoles, return ready (chars will be dropped if queue full)
      // Only block/wait if someone is actually draining the queue
      bool connected = con && con->is_connected();
      bool full = con && con->output_queue().full();
      if (!con || !connected || !full) {
	result = 0xFF;
      }
    }

    cpu_->regs.AF.set_high(result);
}

void XIOS::do_startclock() {
    tick_enabled_.store(true);
}

void XIOS::do_stopclock() {
    tick_enabled_.store(false);
}

void XIOS::do_maxconsole() {
    uint8_t num_consoles = mem_->read_common(0xFF01);
    cpu_->regs.AF.set_high(num_consoles);
}

void XIOS::do_systeminit() {
    uint16_t bnk_version = cpu_->regs.HL.get_pair16();
    std::cerr << "[XIOS] SYSTEMINIT BNK_VERSION=" << (int)bnk_version << "\n";
    std::cerr << "[XIOS] SYSTEMINIT called, IFF1=" << (int)cpu_->regs.IFF1 << "\n";

    // Copy interrupt vectors from bank 0 to all other banks.
    // The Z80 assembly set up bank 0's page 0 with:
    //   - JMP at 0x0000 (warm boot)
    //   - JMP at RST address (debugger)
    //   - JMP at 0x0038 (interrupt handler)
    // We copy the first 64 bytes (0x00-0x3F) which covers all RST vectors.
    int num_banks = mem_->num_banks();
    for (int bank = 1; bank < num_banks; bank++) {
        for (uint16_t addr = 0; addr < 0x40; addr++) {
            uint8_t byte = mem_->read_bank(0, addr);
            mem_->write_bank(bank, addr, byte);
        }
    }
    std::cerr << "[XIOS] Copied page 0 vectors to " << (num_banks - 1) << " banks\n";

    // Initialize consoles
    ConsoleManager::instance().init();

    // Enable timer interrupts
    tick_enabled_.store(true);

    // Debug: dump the COMMONBASE structure to verify GENSYS patching
    // COMMONBASE layout (from bnkxios.asm):
    //   offset 0:  JP DO_BOOT   (not patched)
    //   offset 3:  JP SWTUSER   (patched by GENSYS)
    //   offset 6:  JP SWTSYS    (patched by GENSYS)
    //   offset 9:  JP PDISP     (patched by GENSYS -> XDOS+3)
    //   offset 12: JP XDOS      (patched by GENSYS -> XDOS+6)
    //   offset 15: DW SYSDAT    (patched by GENSYS)
    uint16_t commonbase = cpu_->regs.DE.get_pair16();
    std::cerr << "[XIOS] COMMONBASE at 0x" << std::hex << commonbase << std::dec << ":\n";

    const char* names[] = {"DO_BOOT", "SWTUSER", "SWTSYS", "PDISP", "XDOS"};
    uint16_t addr = commonbase;
    for (int i = 0; i < 5; i++) {
        uint8_t opcode = mem_->fetch_mem(addr);
        uint16_t target = mem_->fetch_mem(addr + 1) | (mem_->fetch_mem(addr + 2) << 8);
        std::cerr << "  " << names[i] << ": "
                  << std::hex << std::setw(2) << std::setfill('0') << (int)opcode
                  << " -> 0x" << std::setw(4) << target << std::dec;
        if (opcode != 0xC3) {
            std::cerr << " [ERROR: expected JP opcode 0xC3]";
        }
        std::cerr << "\n";
        addr += 3;
    }

    // SYSDAT is a 16-bit pointer, not a JP instruction
    uint16_t sysdat = mem_->fetch_mem(addr) | (mem_->fetch_mem(addr + 1) << 8);
    std::cerr << "  SYSDAT: 0x" << std::hex << std::setw(4) << sysdat << std::dec << "\n";
}

void XIOS::do_idle() {
    // Idle procedure - enable interrupts and wait
    static int count = 0;
    if (++count <= 5) {
        std::cerr << "[IDLE] called, IFF1=" << (int)cpu_->regs.IFF1 << "\n";
    }
}

// Commonbase entries - called by XDOS/BNKBDOS for bank switching and dispatch

void XIOS::do_boot() {
    // COLDBOOT returns HL = address of commonbase
    // Calculate XIOS base from PC:
    // When OUT executes in DO_BOOT, PC points to RET at XIOS offset 0x60
    uint16_t pc = cpu_->regs.PC.get_pair16();
    uint16_t xios_base = pc - 0x60;
    uint16_t commonbase = xios_base + XIOS_COMMONBASE;
    cpu_->regs.HL.set_pair16(commonbase);
}

void XIOS::do_wboot() {
    // Warm boot - nothing to do, Z80 code handles return to TMP
}
