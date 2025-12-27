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
    , xios_base_(0x8800)   // Default - below TMP (9100H) but in common memory
    , ldrbios_base_(0x1700) // LDRBIOS for boot phase (matches ldrbios.asm)
    , bdos_stub_(0x0D06)    // MPMLDR's internal BDOS entry
    , current_disk_(0)
    , current_track_(0)
    , current_sector_(0)
    , dma_addr_(0x0080)
    , tick_enabled_(false)
    , preempted_(false)
{
}

// DEPRECATED: PC-based interception has been replaced by I/O port dispatch.
// All XIOS calls now go through port 0xE0 with function code in A register.
// Our port-dispatch code is installed at FB00H (XIOSJMP TBL location).
// These functions are kept for reference but are no longer called.
bool XIOS::is_xios_call(uint16_t pc) const {
    // Check XIOS range (configurable via xios_base_)
    if (pc >= xios_base_ && pc < xios_base_ + 0x100) {
        uint16_t offset = pc - xios_base_;
        // Valid entry point: standard XIOS (multiples of 3 up to IDLE)
        // or commonbase entries (0x4B, 0x4E, 0x51, 0x54, 0x57)
        if ((offset <= XIOS_IDLE) && (offset % 3 == 0)) return true;
        if (offset >= XIOS_COMMONBASE && offset <= XIOS_SYSDAT &&
            (offset - XIOS_COMMONBASE) % 3 == 0) return true;

        // Debug: show what's in memory at non-entry-point XIOS addresses
        static int fc_trace = 0;
        if (offset == 0x80 && fc_trace++ < 3) {
            uint8_t byte = mem_->fetch_mem(pc);
            fprintf(stderr, "[DEBUG] PC=%04X contains 0x%02X\n", pc, byte);
        }
        return false;
    }

    // Note: CD00-CDFF interception disabled - using NUCLEUS layout with BNKXIOS at BA00
    // which contains forwarding stubs that jump to FB00 (XIOS base)

    // Check LDRBIOS range
    if (pc >= ldrbios_base_ && pc < ldrbios_base_ + 0x100) {
        uint16_t offset = pc - ldrbios_base_;
        // LDRBIOS only has standard entries up to SECTRAN (0x30)
        return (offset <= XIOS_SECTRAN) && (offset % 3 == 0);
    }

    return false;
}

bool XIOS::handle_call(uint16_t pc) {
    if (!is_xios_call(pc)) return false;

    // Compute offset - works for XIOS and LDRBIOS
    // BNKXIOS calls now come via FB00 due to forwarding stub
    uint16_t offset;
    bool is_ldrbios = (pc >= ldrbios_base_ && pc < ldrbios_base_ + 0x100);

    if (pc >= xios_base_ && pc < xios_base_ + 0x100) {
        offset = pc - xios_base_;
    } else {
        offset = pc - ldrbios_base_;
    }

    // For LDRBIOS, don't intercept SELDSK or SECTRAN - let LDRBIOS handle these
    // using its own DPH and translation tables. The emulator's do_seldsk() uses
    // XIOS addresses which would corrupt memory during boot.
    if (is_ldrbios && (offset == XIOS_SELDSK || offset == XIOS_SECTRAN)) {
        return false;  // Let LDRBIOS code run
    }

    // Trace extended XIOS calls (after SECTRAN)
    static const char* names[] = {
        "BOOT", "WBOOT", "CONST", "CONIN", "CONOUT", "LIST", "PUNCH", "READER",
        "HOME", "SELDSK", "SETTRK", "SETSEC", "SETDMA", "READ", "WRITE", "LISTST",
        "SECTRAN", "SELMEM", "POLLDEV", "STARTCLK", "STOPCLK", "EXITRGN", "MAXCON", "SYSINIT", "IDLE"
    };
    int idx = offset / 3;
    // Trace ALL calls (both XIOS and LDRBIOS) with registers
    static int trace_count = 0;
    // Trace all XIOS calls (including BNKXIOS)
    if (trace_count++ < 200) {
        uint16_t sp = cpu_->regs.SP.get_pair16();
        uint16_t hl = cpu_->regs.HL.get_pair16();
        uint16_t ret_lo = mem_->fetch_mem(sp);
        uint16_t ret_hi = mem_->fetch_mem(sp + 1);
        uint16_t ret_addr = ret_lo | (ret_hi << 8);
        std::cout << (is_ldrbios ? "[LDRBIOS] " : "[XIOS] ")
                  << (idx < 25 ? names[idx] : "???")
                  << " @ 0x" << std::hex << pc
                  << " SP=0x" << sp
                  << " HL=0x" << hl
                  << " stack[0]=0x" << ret_addr << std::dec << std::endl;

        // Show what's at the call site (before the CALL instruction pushed to stack)
        // CALL is at ret_addr - 3
        if (trace_count < 10) {
            uint16_t call_addr = ret_addr - 3;
            uint8_t c0 = mem_->fetch_mem(call_addr);
            uint8_t c1 = mem_->fetch_mem(call_addr + 1);
            uint8_t c2 = mem_->fetch_mem(call_addr + 2);
            fprintf(stderr, "[CALLSITE] at %04X: %02X %02X %02X (expect CD xx FC)\n",
                    call_addr, c0, c1, c2);
        }
    }

    // Dump memory at BF80-BFA0 before first BOOT call
    static bool dumped_bf80 = false;
    if (offset == XIOS_BOOT && !dumped_bf80) {
        dumped_bf80 = true;
        fprintf(stderr, "[BOOT] Memory dump at BF80-BFA0 BEFORE first BOOT:\n");
        for (uint16_t addr = 0xBF80; addr < 0xBFA0; addr += 16) {
            fprintf(stderr, "[BOOT] %04X: ", addr);
            for (int i = 0; i < 16; i++) {
                fprintf(stderr, "%02X ", mem_->fetch_mem(addr + i));
            }
            fprintf(stderr, "\n");
        }
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

        // Commonbase entries (patched by GENSYS, called by XDOS/BNKBDOS)
        case XIOS_COMMONBASE: do_boot(); break;  // Returns commonbase address
        case XIOS_SWTUSER:    do_swtuser(); break;  // Switch to user bank
        case XIOS_SWTSYS:     do_swtsys(); break;   // Switch to system bank
        case XIOS_PDISP:      do_pdisp(); break;    // Process dispatcher
        case XIOS_XDOSENT:    do_xdosent(); break;  // XDOS entry
        case XIOS_SYSDAT:     do_sysdat(); break;   // System data pointer

        default:
            return false;  // Unknown entry
    }

    return true;
}

void XIOS::handle_port_dispatch(uint8_t func) {
    // Port-based dispatch: function offset in B register
    // The Z80 code does: LD B, func; OUT (0xE0), A; RET
    // So we don't call do_ret() here - the Z80 has its own RET

    // Temporarily set skip_ret flag so handlers don't do RET
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
            std::cerr << "[XIOS PORT] Unknown function 0x" << std::hex << (int)func << std::dec << std::endl;
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

    // Debug: show return addresses in XIOS range (FB00 for NUCLEUS)
    if ((ret_addr >= 0xFB00 && ret_addr < 0xFC00)) {
        fprintf(stderr, "[XIOS do_ret] SP=%04X returning to %04X\n", sp, ret_addr);
    }

    // Trace instruction at return address (for BOOT debugging)
    static int ret_trace = 0;
    if (ret_addr >= 0xBF00 && ret_addr < 0xC000 && ret_trace++ < 5) {
        uint8_t b0 = mem_->fetch_mem(ret_addr);
        uint8_t b1 = mem_->fetch_mem(ret_addr + 1);
        uint8_t b2 = mem_->fetch_mem(ret_addr + 2);
        uint8_t b3 = mem_->fetch_mem(ret_addr + 3);
        fprintf(stderr, "[do_ret trace] Instruction at %04X: %02X %02X %02X %02X\n",
                ret_addr, b0, b1, b2, b3);
    }
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
    // D = console number (for XIOS), C = character
    // During LDRBIOS boot (PC < 0x8000), always use console 0
    // During XIOS (PC >= 0x8000), use D register for console number
    uint16_t pc = cpu_->regs.PC.get_pair16();
    uint8_t console = (pc >= 0x8000) ? cpu_->regs.DE.get_high() : 0;
    uint8_t ch = cpu_->regs.BC.get_low();

    // For local mode: output post-boot console characters to stdout
    // Only output from high memory (MP/M II kernel) to avoid double output
    if (pc >= 0x8000 && (ch >= 0x20 || ch == '\r' || ch == '\n')) {
        std::cout << (char)ch << std::flush;
    }

    // Get the specified console
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

    // Debug: trace disk selection
    std::cout << "[SELDSK] disk=" << (int)disk << " (" << (char)('A' + disk) << ":)" << std::endl;

    // Check if disk is valid (mounted)
    if (!DiskSystem::instance().select(disk)) {
        std::cout << "[SELDSK] disk " << (char)('A' + disk) << ": not mounted, returning error" << std::endl;
        if (!skip_ret_) {
            cpu_->regs.HL.set_pair16(0x0000);  // Error - no such disk (only for PC-based)
        }
        do_ret();
        return;
    }

    current_disk_ = disk;

    // For port dispatch (LDRBIOS), don't modify HL - caller has its own DPH tables
    if (skip_ret_) {
        return;  // Just track disk selection, LDRBIOS calculates its own DPH
    }

    // Set up DPH and DPB structures in memory
    // DPH table is at XIOS_BASE + 0x100 (0xFD00 by default)
    // Each DPH is 16 bytes, DPB is 15 bytes
    uint16_t dph_addr = xios_base_ + 0x100 + (disk * 32);  // 32 bytes per disk (DPH+DPB)
    uint16_t dpb_addr = dph_addr + 16;
    uint16_t dirbuf_addr = xios_base_ + 0x80;  // Common directory buffer

    // Get disk parameters
    Disk* dsk = DiskSystem::instance().get(disk);
    if (!dsk) {
        cpu_->regs.HL.set_pair16(0x0000);
        do_ret();
        return;
    }

    // Write DPH (16 bytes)
    // +0: XLT (sector translation table, 0 = no translation)
    mem_->store_mem(dph_addr + 0, 0x00);
    mem_->store_mem(dph_addr + 1, 0x00);
    // +2: scratch (6 bytes)
    for (int i = 2; i < 8; i++) mem_->store_mem(dph_addr + i, 0x00);
    // +8: DIRBUF
    mem_->store_mem(dph_addr + 8, dirbuf_addr & 0xFF);
    mem_->store_mem(dph_addr + 9, (dirbuf_addr >> 8) & 0xFF);
    // +10: DPB pointer
    mem_->store_mem(dph_addr + 10, dpb_addr & 0xFF);
    mem_->store_mem(dph_addr + 11, (dpb_addr >> 8) & 0xFF);
    // +12: CSV (0 = no check)
    mem_->store_mem(dph_addr + 12, 0x00);
    mem_->store_mem(dph_addr + 13, 0x00);
    // +14: ALV (allocation vector at DPB+15)
    uint16_t alv_addr = dpb_addr + 15;
    mem_->store_mem(dph_addr + 14, alv_addr & 0xFF);
    mem_->store_mem(dph_addr + 15, (alv_addr >> 8) & 0xFF);

    // Write DPB (15 bytes) from disk's detected format
    const DiskParameterBlock& diskdpb = dsk->dpb();
    uint16_t spt = diskdpb.spt;
    uint8_t bsh = diskdpb.bsh;
    uint8_t blm = diskdpb.blm;
    uint8_t exm = diskdpb.exm;
    uint16_t dsm = diskdpb.dsm;
    uint16_t drm = diskdpb.drm;
    uint8_t al0 = diskdpb.al0;
    uint8_t al1 = diskdpb.al1;
    uint16_t cks = diskdpb.cks;
    uint16_t off = diskdpb.off;

    std::cout << "[SELDSK] DPB: spt=" << spt << " bsh=" << (int)bsh
              << " format=" << (int)dsk->format() << std::endl;

    mem_->store_mem(dpb_addr + 0, spt & 0xFF);
    mem_->store_mem(dpb_addr + 1, (spt >> 8) & 0xFF);
    mem_->store_mem(dpb_addr + 2, bsh);
    mem_->store_mem(dpb_addr + 3, blm);
    mem_->store_mem(dpb_addr + 4, exm);
    mem_->store_mem(dpb_addr + 5, dsm & 0xFF);
    mem_->store_mem(dpb_addr + 6, (dsm >> 8) & 0xFF);
    mem_->store_mem(dpb_addr + 7, drm & 0xFF);
    mem_->store_mem(dpb_addr + 8, (drm >> 8) & 0xFF);
    mem_->store_mem(dpb_addr + 9, al0);
    mem_->store_mem(dpb_addr + 10, al1);
    mem_->store_mem(dpb_addr + 11, cks & 0xFF);
    mem_->store_mem(dpb_addr + 12, (cks >> 8) & 0xFF);
    mem_->store_mem(dpb_addr + 13, off & 0xFF);
    mem_->store_mem(dpb_addr + 14, (off >> 8) & 0xFF);

    cpu_->regs.HL.set_pair16(dph_addr);
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
    uint16_t old_dma = dma_addr_;
    // For port dispatch: assembly copies BC to HL before OUT
    // For PC-based dispatch (legacy): BC = DMA address
    dma_addr_ = skip_ret_ ? cpu_->regs.HL.get_pair16() : cpu_->regs.BC.get_pair16();
    std::cerr << "[SETDMA] addr=0x" << std::hex << dma_addr_
              << " (was 0x" << old_dma << ")" << std::dec << std::endl;
    do_ret();
}

void XIOS::do_read() {
    std::cerr << "[do_read] Called: trk=" << current_track_
              << " sec=" << current_sector_
              << " dma=0x" << std::hex << dma_addr_ << std::dec << std::endl;

    // Set up disk system with current parameters
    DiskSystem::instance().set_track(current_track_);
    DiskSystem::instance().set_sector(current_sector_);
    DiskSystem::instance().set_dma(dma_addr_);

    // Perform read
    int result = DiskSystem::instance().read(mem_);

    // Trace reads to high memory (where system modules are loaded)
    static int read_trace = 0;
    if (read_trace++ < 300) {
        std::cerr << "[XIOS READ] dma=0x" << std::hex << dma_addr_
                  << " trk=" << std::dec << current_track_
                  << " sec=" << current_sector_
                  << " result=" << result << std::endl;
    }

    // Also trace errors
    if (result != 0) {
        std::cout << "[DISK ERROR] trk=" << current_track_
                  << " sec=" << current_sector_
                  << " result=" << result << std::endl;
    }

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
        std::cout << "[SECTRAN] log=" << logical << " xlat=0x" << std::hex << xlat_table
                  << " -> phys=" << std::dec << physical << std::endl;
    }

    cpu_->regs.HL.set_pair16(physical);
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

// Commonbase entries - called by XDOS/BNKBDOS for bank switching and dispatch

void XIOS::do_swtuser() {
    // Switch to user bank
    // BC contains memory descriptor address
    // The descriptor's bank field tells us which bank to switch to
    uint16_t desc_addr = cpu_->regs.BC.get_pair16();
    if (desc_addr != 0) {
        uint8_t bank = mem_->fetch_mem(desc_addr + 3);
        mem_->select_bank(bank);
    }
    do_ret();
}

void XIOS::do_swtsys() {
    // Switch to system bank (bank 0)
    mem_->select_bank(0);
    do_ret();
}

void XIOS::do_pdisp() {
    // Process dispatcher entry point
    // This is called when no processes are ready
    // For our emulator, just return (the Z80 thread will continue polling)
    do_ret();
}

void XIOS::do_xdosent() {
    // XDOS entry point
    // Called with C = function number, DE = parameter
    // For now, just return - XDOS is loaded and handles this itself
    do_ret();
}

void XIOS::do_sysdat() {
    // Return pointer to system data area (SYSDAT) in HL
    // SYSDAT is at FF00H
    cpu_->regs.HL.set_pair16(0xFF00);
    do_ret();
}

// xios_port.bin - Z80 code for port-based XIOS dispatch at FB00
// Generated from asm/xios_port.bin - do not edit manually
static const uint8_t xios_port_code[] = {
    0xc3, 0x5c, 0xfb, 0xc3, 0x61, 0xfb, 0xc3, 0x66, 0xfb, 0xc3, 0x6b, 0xfb,
    0xc3, 0x70, 0xfb, 0xc3, 0x75, 0xfb, 0xc3, 0x7a, 0xfb, 0xc3, 0x7f, 0xfb,
    0xc3, 0x84, 0xfb, 0xc3, 0x89, 0xfb, 0xc3, 0x8e, 0xfb, 0xc3, 0x95, 0xfb,
    0xc3, 0x9c, 0xfb, 0xc3, 0xa3, 0xfb, 0xc3, 0xa8, 0xfb, 0xc3, 0xad, 0xfb,
    0xc3, 0xb2, 0xfb, 0xc3, 0xb9, 0xfb, 0xc3, 0xbe, 0xfb, 0xc3, 0xc3, 0xfb,
    0xc3, 0xc8, 0xfb, 0xc3, 0xcd, 0xfb, 0xc3, 0xd2, 0xfb, 0xc3, 0xd7, 0xfb,
    0xc3, 0xdc, 0xfb, 0xc3, 0xe1, 0xfb, 0xc3, 0xe6, 0xfb, 0xc3, 0xeb, 0xfb,
    0xc3, 0xf0, 0xfb, 0xc3, 0xf5, 0xfb, 0x00, 0xff, 0x3e, 0x00, 0xd3, 0xe0,
    0xc9, 0x3e, 0x03, 0xd3, 0xe0, 0xc9, 0x3e, 0x06, 0xd3, 0xe0, 0xc9, 0x3e,
    0x09, 0xd3, 0xe0, 0xc9, 0x3e, 0x0c, 0xd3, 0xe0, 0xc9, 0x3e, 0x0f, 0xd3,
    0xe0, 0xc9, 0x3e, 0x12, 0xd3, 0xe0, 0xc9, 0x3e, 0x15, 0xd3, 0xe0, 0xc9,
    0x3e, 0x18, 0xd3, 0xe0, 0xc9, 0x3e, 0x1b, 0xd3, 0xe0, 0xc9, 0x60, 0x69,
    0x3e, 0x1e, 0xd3, 0xe0, 0xc9, 0x60, 0x69, 0x3e, 0x21, 0xd3, 0xe0, 0xc9,
    0x60, 0x69, 0x3e, 0x24, 0xd3, 0xe0, 0xc9, 0x3e, 0x27, 0xd3, 0xe0, 0xc9,
    0x3e, 0x2a, 0xd3, 0xe0, 0xc9, 0x3e, 0x2d, 0xd3, 0xe0, 0xc9, 0x60, 0x69,
    0x3e, 0x30, 0xd3, 0xe0, 0xc9, 0x3e, 0x33, 0xd3, 0xe0, 0xc9, 0x3e, 0x36,
    0xd3, 0xe0, 0xc9, 0x3e, 0x39, 0xd3, 0xe0, 0xc9, 0x3e, 0x3c, 0xd3, 0xe0,
    0xc9, 0x3e, 0x3f, 0xd3, 0xe0, 0xc9, 0x3e, 0x42, 0xd3, 0xe0, 0xc9, 0x3e,
    0x45, 0xd3, 0xe0, 0xc9, 0x3e, 0x48, 0xd3, 0xe0, 0xc9, 0x3e, 0x4b, 0xd3,
    0xe0, 0xc9, 0x3e, 0x4e, 0xd3, 0xe0, 0xc9, 0x3e, 0x51, 0xd3, 0xe0, 0xc9,
    0x3e, 0x54, 0xd3, 0xe0, 0xc9, 0x3e, 0x57, 0xd3, 0xe0, 0xc9, 0x00, 0x3c,
    0x00
};
static const size_t xios_port_code_len = sizeof(xios_port_code);

void XIOS::patch_bnkxios(uint16_t explicit_bnkxios_addr) {
    // Install port-dispatch XIOS at FB00 where XIOSJMP TBL is located
    // MPMLDR creates XIOSJMP TBL at FB00H, so we install our dispatcher there
    // The dispatcher forwards XIOS calls to the emulator via port I/O

    static bool xios_installed = false;
    if (xios_installed) return;

    std::cerr << "[patch_bnkxios] Installing port-dispatch XIOS at FB00H" << std::endl;

    // Install xios_port code at FB00 (XIOSJMP TBL location)
    for (size_t i = 0; i < xios_port_code_len; i++) {
        mem_->store_mem(0xFB00 + i, xios_port_code[i]);
    }

    // Determine BNKXIOS address - use explicit if provided, else try 0xFF0D
    uint16_t bnkxios_addr = explicit_bnkxios_addr;
    if (bnkxios_addr == 0) {
        // Try to read from 0xFF0D (page number stored by loader)
        uint8_t bnkxios_page = mem_->fetch_mem(0xFF0D);
        bnkxios_addr = static_cast<uint16_t>(bnkxios_page) << 8;
        std::cerr << "[patch_bnkxios] 0xFF0D=" << std::hex << (int)bnkxios_page
                  << "H -> BNKXIOS addr=" << bnkxios_addr << "H" << std::dec << std::endl;
    } else {
        std::cerr << "[patch_bnkxios] Using explicit BNKXIOS addr=" << std::hex
                  << bnkxios_addr << "H" << std::dec << std::endl;
    }

    if (bnkxios_addr != 0 && bnkxios_addr != 0xFB00) {
        std::cerr << "[patch_bnkxios] Patching BNKXIOS at " << std::hex << bnkxios_addr
                  << "H to forward to FB00H" << std::dec << std::endl;

        // Dump what's there before patching
        std::cerr << "[patch_bnkxios] BNKXIOS before: ";
        for (int i = 0; i < 12; i++) {
            std::cerr << std::hex << (int)mem_->fetch_mem(bnkxios_addr + i) << " ";
        }
        std::cerr << std::dec << std::endl;

        // Patch each jump table entry (30 entries for standard XIOS + extended)
        // Each entry is 3 bytes: JP xxxx -> JP FB00+offset
        // Standard BIOS: 17 entries (0x00-0x30), MP/M extensions: 13 more (0x33-0x57)
        for (int entry = 0; entry <= 30; entry++) {
            uint16_t offset = entry * 3;
            uint16_t target = 0xFB00 + offset;

            // Write JP target (C3 lo hi)
            mem_->store_mem(bnkxios_addr + offset, 0xC3);        // JP
            mem_->store_mem(bnkxios_addr + offset + 1, target & 0xFF);  // low byte
            mem_->store_mem(bnkxios_addr + offset + 2, target >> 8);    // high byte
        }

        // Dump after patching
        std::cerr << "[patch_bnkxios] BNKXIOS after:  ";
        for (int i = 0; i < 12; i++) {
            std::cerr << std::hex << (int)mem_->fetch_mem(bnkxios_addr + i) << " ";
        }
        std::cerr << std::dec << std::endl;
    }

    // Cache the BNKXIOS address for use by do_boot()
    bnkxios_addr_ = bnkxios_addr;

    xios_installed = true;

    // Verify FB00 installation
    std::cerr << "[patch_bnkxios] Installed " << xios_port_code_len
              << " bytes at FB00H, first bytes: "
              << std::hex << (int)mem_->fetch_mem(0xFB00) << " "
              << (int)mem_->fetch_mem(0xFB01) << " "
              << (int)mem_->fetch_mem(0xFB02) << std::dec << std::endl;
}

void XIOS::do_boot() {
    // For MP/M II, COLDBOOT (offset 0) returns HL = address of commonbase
    // The commonbase structure is inside BNKXIOS at offset 0x4E (SWTUSER), etc.

    // Ensure BNKXIOS is patched
    patch_bnkxios();

    // Get BNKXIOS address from cached value (set by patch_bnkxios)
    uint16_t bnkxios_addr = bnkxios_addr_;
    if (bnkxios_addr == 0) {
        // Fallback to reading from 0xFF0D
        uint8_t bnkxios_page = mem_->fetch_mem(0xFF0D);
        bnkxios_addr = static_cast<uint16_t>(bnkxios_page) << 8;
    }

    // Commonbase structure starts at SWTUSER offset within BNKXIOS
    uint16_t commonbase = bnkxios_addr + XIOS_SWTUSER;  // e.g., CD00+4E = CD4E

    std::cerr << "[do_boot] BNKXIOS=" << std::hex << bnkxios_addr
              << "H commonbase=" << commonbase << "H" << std::dec << std::endl;

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
