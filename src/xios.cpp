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

XIOS::XIOS(qkz80* cpu, BankedMemory* mem)
    : cpu_(cpu)
    , mem_(mem)
    , xios_base_(0x8800)   // Default - below TMP (9100H) but in common memory
    , current_disk_(0)
    , current_track_(0)
    , current_sector_(0)
    , dma_addr_(0x0080)
    , tick_enabled_(false)
    , preempted_(false)
{
}

void XIOS::handle_port_dispatch(uint8_t func) {
    // Port-based dispatch: function offset written to port E0
    // The A register contains the XIOS function offset (0, 3, 6, 9, etc.)

    // Debug: trace calls, especially BOOT and after patch
    static int total_calls = 0;
    static bool patch_happened = false;
    total_calls++;

    // Always trace BOOT calls
    if (func == XIOS_BOOT) {
        std::cerr << "[PORT DISPATCH] BOOT called! (call #" << total_calls << ")\n";
        patch_happened = true;
    }

    // Trace first 30 calls, or first 30 after patch
    static int post_patch_count = 0;
    if (total_calls <= 30 || (patch_happened && post_patch_count++ < 30)) {
        std::cerr << "[PORT DISPATCH] func=0x" << std::hex << (int)func
                  << " (call #" << std::dec << total_calls << ")"
                  << (patch_happened ? " [post-patch]" : "") << "\n";
    }

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

// Console I/O - B register contains console number (MP/M II convention)
void XIOS::do_const() {
    uint8_t console = cpu_->regs.BC.get_high();  // B = console number
    Console* con = ConsoleManager::instance().get(console);

    uint8_t status = 0x00;
    if (con) {
        status = con->const_status();
    }
    // Debug: show when input is available
    static int last_status[8] = {0};
    if (console < 8 && status != last_status[console]) {
        std::cerr << "[CONST] con=" << (int)console << " status=" << (status ? "READY" : "empty") << "\n";
        last_status[console] = status;
    }
    cpu_->regs.AF.set_high(status);
    do_ret();
}

void XIOS::do_conin() {
    uint8_t console = cpu_->regs.BC.get_high();  // B = console number
    Console* con = ConsoleManager::instance().get(console);

    uint8_t ch = 0x1A;  // EOF default
    if (con) {
        ch = con->read_char();
        if (ch != 0x00) {
            std::cerr << "[CONIN] con=" << (int)console << " ch=0x" << std::hex << (int)ch << std::dec << "\n";
        }
    }
    cpu_->regs.AF.set_high(ch);
    do_ret();
}

void XIOS::do_conout() {
    // B = console number (MP/M II), C = character
    // For boot phase (LDRBIOS/MPMLDR at PC < 0x8000), always use console 0
    // because B register contains garbage during boot
    // Once MP/M II is running (code at 0x8000+), use B register
    uint16_t pc = cpu_->regs.PC.get_pair16();
    uint8_t console = (pc < 0x8000) ? 0 : cpu_->regs.BC.get_high();
    uint8_t ch = cpu_->regs.BC.get_low();

    // Debug: show first few CONOUT calls to verify
    static int debug_count = 0;
    if (debug_count++ < 20) {
        std::cerr << "[do_conout] skip_ret=" << skip_ret_ << " pc=0x" << std::hex << pc
                  << " console=" << std::dec << (int)console << " ch=0x" << std::hex << (int)ch << std::dec << "\n";
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
    // For port dispatch: assembly copies BC to HL before OUT
    // For PC-based dispatch (legacy): BC = DMA address
    dma_addr_ = skip_ret_ ? cpu_->regs.HL.get_pair16() : cpu_->regs.BC.get_pair16();
    do_ret();
}

void XIOS::do_read() {
    // Set up disk system with current parameters
    DiskSystem::instance().set_track(current_track_);
    DiskSystem::instance().set_sector(current_sector_);
    DiskSystem::instance().set_dma(dma_addr_);

    // Perform read
    int result = DiskSystem::instance().read(mem_);

    // Debug reads - show all reads with unique DMA or track changes
    static int read_count = 0;
    static uint16_t last_dma = 0;
    static uint16_t last_trk = 0xFFFF;
    read_count++;
    if (current_track_ != last_trk || dma_addr_ != last_dma || read_count < 5 || read_count % 50 == 0) {
        std::cerr << "[DISK READ #" << read_count << "] trk=" << current_track_
                  << " sec=" << current_sector_
                  << " dma=0x" << std::hex << dma_addr_ << std::dec
                  << " result=" << result << std::endl;
        last_dma = dma_addr_;
        last_trk = current_track_;
    }

    // Report disk errors
    if (result != 0) {
        std::cerr << "[DISK ERROR] read trk=" << current_track_
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

    // Debug: trace bank switches
    static int switch_count = 0;
    if (switch_count++ < 20) {
        std::cerr << "[SELMEMORY] desc=0x" << std::hex << desc_addr
                  << " bank=" << std::dec << (int)bank
                  << " PC=0x" << std::hex << cpu_->regs.PC.get_pair16()
                  << " SP=0x" << cpu_->regs.SP.get_pair16() << std::dec << "\n";
    }

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

void XIOS::do_boot() {
    // For MP/M II, COLDBOOT (offset 0) returns HL = address of commonbase
    // The commonbase structure is in XIOSJMP (at FC00H)
    // NOTE: Memory at 0xFF0C gets corrupted, so use hardcoded FC00

    uint16_t xiosjmp_addr = 0xFC00;  // Hardcoded - memory at FF0C is corrupted

    // Commonbase structure starts at offset 0x4B in XIOSJMP
    uint16_t commonbase = xiosjmp_addr + XIOS_COMMONBASE;  // FC00+4B = FC4B

    // Debug: trace the return address (who's calling BOOT?)
    static int boot_count = 0;
    uint16_t sp = cpu_->regs.SP.get_pair16();
    uint8_t lo = mem_->fetch_mem(sp);
    uint8_t hi = mem_->fetch_mem(sp + 1);
    uint16_t caller = (hi << 8) | lo;
    boot_count++;

    // Trace every 100th call, or first 5, or calls 2390-2400
    if (boot_count <= 5 || (boot_count % 100 == 0) || (boot_count >= 2390 && boot_count <= 2400)) {
        std::cerr << "[do_boot #" << std::dec << boot_count << "] caller=" << std::hex << caller
                  << "H SP=" << sp << "H" << std::dec << std::endl;
    }

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
            // DE points to FCB, search directory for file
            {
                uint16_t fcb = de;
                bdos_fcb_ = fcb;

                // Get filename from FCB (bytes 1-8 = name, 9-11 = type)
                char filename[13];
                for (int i = 0; i < 8; i++) {
                    filename[i] = mem_->fetch_mem(fcb + 1 + i) & 0x7F;
                }
                filename[8] = '.';
                for (int i = 0; i < 3; i++) {
                    filename[9 + i] = mem_->fetch_mem(fcb + 9 + i) & 0x7F;
                }
                filename[12] = '\0';

                std::cerr << "[BDOS 15] Open file: " << filename << "\n";

                // Search directory for file
                // Directory is at track 2 (after system tracks) for hd1k
                Disk* dsk = DiskSystem::instance().get(current_disk_);
                if (!dsk) {
                    cpu_->regs.AF.set_high(0xFF);
                    break;
                }

                // Read directory sectors and search for file
                uint8_t dirbuf[512];
                bool found = false;

                // Directory starts at track 2 (dpb.off), scan first 16 sectors
                for (int sec = 0; sec < 16 && !found; sec++) {
                    DiskSystem::instance().set_track(2);
                    DiskSystem::instance().set_sector(sec);
                    DiskSystem::instance().set_dma(0xF000);  // Temp buffer in high mem
                    DiskSystem::instance().read(mem_);

                    // Copy from memory to local buffer
                    for (int i = 0; i < 512; i++) {
                        dirbuf[i] = mem_->fetch_mem(0xF000 + i);
                    }

                    // Each sector has 16 directory entries (32 bytes each)
                    for (int entry = 0; entry < 16 && !found; entry++) {
                        uint8_t* ent = &dirbuf[entry * 32];

                        // Skip deleted entries
                        if (ent[0] == 0xE5) continue;
                        // Stop at end of directory
                        if (ent[0] == 0x00 && ent[1] == 0x00) break;

                        // Compare filename (bytes 1-8) and type (bytes 9-11)
                        bool match = true;
                        for (int i = 1; i <= 11; i++) {
                            uint8_t fcb_char = mem_->fetch_mem(fcb + i) & 0x7F;
                            uint8_t dir_char = ent[i] & 0x7F;
                            // Skip comparison if FCB has '?' wildcard
                            if (fcb_char == '?') continue;
                            if (fcb_char != dir_char) {
                                match = false;
                                break;
                            }
                        }

                        if (match && ent[12] == 0) {  // Extent 0 only
                            std::cerr << "[BDOS 15] Found file at sector " << sec
                                      << " entry " << entry << "\n";

                            // Copy directory entry to FCB
                            for (int i = 0; i < 32; i++) {
                                mem_->store_mem(fcb + i, ent[i]);
                            }
                            // Clear CR (current record)
                            mem_->store_mem(fcb + 32, 0);

                            bdos_file_offset_ = 0;
                            bdos_file_open_ = true;
                            found = true;
                        }
                    }
                }

                cpu_->regs.AF.set_high(found ? 0x00 : 0xFF);
            }
            break;

        case 20: // Read sequential
            // DE points to FCB, read 128 bytes to DMA
            {
                if (!bdos_file_open_) {
                    cpu_->regs.AF.set_high(1);  // EOF
                    break;
                }

                uint16_t fcb = de;
                uint8_t cr = mem_->fetch_mem(fcb + 32);  // Current record
                uint8_t rc = mem_->fetch_mem(fcb + 15);  // Record count in extent

                if (cr >= rc) {
                    // Need next extent - for now, just return EOF
                    // TODO: Load next extent
                    std::cerr << "[BDOS 20] EOF at CR=" << (int)cr << " RC=" << (int)rc << "\n";
                    cpu_->regs.AF.set_high(1);  // EOF
                    break;
                }

                // Calculate block and offset within block
                // For hd1k: BSH=4 (16KB blocks), BLM=0x7F
                // Each block has 128 records
                int block_in_extent = cr / 128;  // Which block in allocation map
                int record_in_block = cr % 128;  // Record offset within block

                // Get block number from allocation map (FCB bytes 16-31)
                // For hd1k (DSM>255), allocation is 2 bytes per entry
                uint16_t alloc_lo = mem_->fetch_mem(fcb + 16 + block_in_extent * 2);
                uint16_t alloc_hi = mem_->fetch_mem(fcb + 16 + block_in_extent * 2 + 1);
                uint16_t block_num = alloc_lo | (alloc_hi << 8);

                if (block_num == 0) {
                    std::cerr << "[BDOS 20] No block allocated at index " << block_in_extent << "\n";
                    cpu_->regs.AF.set_high(1);  // EOF
                    break;
                }

                // Calculate track and sector for this record
                // For hd1k: 16 sectors/track, 512 bytes/sector = 8KB/track
                // Block size = 16KB = 2 tracks
                // OFF=2 (reserved tracks)
                int bytes_per_track = 16 * 512;
                int block_offset = block_num * (16 * 1024);  // Block size = 16KB
                int record_offset = record_in_block * 128;
                int total_offset = block_offset + record_offset;

                int track = 2 + (total_offset / bytes_per_track);  // +2 for reserved tracks
                int sector = (total_offset % bytes_per_track) / 512;
                int offset_in_sector = (total_offset % bytes_per_track) % 512;

                static int read_trace = 0;
                if (read_trace++ < 20) {
                    std::cerr << "[BDOS 20] CR=" << (int)cr << " block=" << block_num
                              << " trk=" << track << " sec=" << sector
                              << " off=" << offset_in_sector << "\n";
                }

                // Read the sector
                DiskSystem::instance().set_track(track);
                DiskSystem::instance().set_sector(sector);
                DiskSystem::instance().set_dma(0xF000);  // Temp buffer
                DiskSystem::instance().read(mem_);

                // Copy 128 bytes to DMA buffer
                for (int i = 0; i < 128; i++) {
                    uint8_t byte = mem_->fetch_mem(0xF000 + offset_in_sector + i);
                    mem_->store_mem(bdos_dma_ + i, byte);
                }

                // Increment current record
                mem_->store_mem(fcb + 32, cr + 1);

                cpu_->regs.AF.set_high(0);  // Success
            }
            break;

        case 26: // Set DMA address
            bdos_dma_ = de;
            break;

        default:
            // Unknown function - just return
            break;
    }

    do_ret();
}
