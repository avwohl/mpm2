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
    // Temporarily set skip_ret flag so handlers don't do RET
    skip_ret_ = true;

    // Trace function dispatches
    static int dispatch_count = 0;
    dispatch_count++;
    // Only trace SWTUSER, SWTSYS, POLLDEVICE, CONIN, and first 200 calls
    if (func == XIOS_SWTUSER || func == XIOS_SWTSYS || func == XIOS_POLLDEVICE ||
        func == XIOS_CONIN || dispatch_count <= 200) {
        std::cerr << "[DISP #" << dispatch_count << "] func=0x" << std::hex << (int)func
                  << " PC=0x" << cpu_->regs.PC.get_pair16() << std::dec << "\n";
    }

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

// Console I/O - D register contains console number (MP/M II XIOS convention)
void XIOS::do_const() {
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    Console* con = ConsoleManager::instance().get(console);

    uint8_t status = 0x00;
    if (con) {
        status = con->const_status();
    }

    // Trace console status polling
    static int const_count = 0;
    const_count++;
    if (const_count <= 20) {
        std::cerr << "[CONST #" << const_count << "] console=" << (int)console
                  << " status=" << (status ? "ready" : "empty") << "\n";
    }

    cpu_->regs.AF.set_high(status);
    do_ret();
}

void XIOS::do_conin() {
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    Console* con = ConsoleManager::instance().get(console);

    uint8_t ch = 0x1A;  // EOF default
    if (con) {
        ch = con->read_char();
    }
    cpu_->regs.AF.set_high(ch);
    do_ret();
}

void XIOS::do_conout() {
    // D = console number (MP/M II XIOS convention), C = character
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    uint8_t ch = cpu_->regs.BC.get_low();        // C = character

    // Trace all console output
    static int out_count = 0;
    out_count++;
    if (out_count <= 200) {
        std::cerr << "[CONOUT #" << out_count << "] console=" << (int)console
                  << " ch=0x" << std::hex << (int)ch << std::dec
                  << " '" << (char)(ch >= 32 && ch < 127 ? ch : '.') << "'\n";
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
    // Select disk - validates disk and returns success/error in A
    // Z80 code calculates DPH address from its own DPH_TABLE
    uint8_t disk = cpu_->regs.BC.get_low();  // C = disk number

    // Check if disk is valid (mounted and within range)
    if (disk >= 4 || !DiskSystem::instance().select(disk)) {
        std::cerr << "[SELDSK] disk=" << (int)disk << " -> ERROR (not mounted)\n";
        cpu_->regs.AF.set_high(0xFF);  // Return error
        do_ret();
        return;
    }

    current_disk_ = disk;
    std::cerr << "[SELDSK] disk=" << (int)disk << " -> OK\n";
    cpu_->regs.AF.set_high(0);  // Return success
    do_ret();
}

void XIOS::do_settrk() {
    // For port dispatch: assembly copies BC to HL before OUT
    // For PC-based dispatch (legacy): BC = track number
    current_track_ = skip_ret_ ? cpu_->regs.HL.get_pair16() : cpu_->regs.BC.get_pair16();

    static int trk_count = 0;
    trk_count++;
    if (trk_count <= 20 || current_track_ > 1024) {
        std::cerr << "[SETTRK #" << trk_count << "] track=" << current_track_
                  << " HL=0x" << std::hex << cpu_->regs.HL.get_pair16()
                  << " BC=0x" << cpu_->regs.BC.get_pair16() << std::dec << "\n";
    }
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
    uint8_t d_base = mem_->fetch_mem(desc_addr + 0);
    uint8_t d_size = mem_->fetch_mem(desc_addr + 1);
    uint8_t d_attr = mem_->fetch_mem(desc_addr + 2);
    uint8_t bank = mem_->fetch_mem(desc_addr + 3);

    static int selmem_count = 0;
    static uint16_t last_desc = 0;
    selmem_count++;

    // Log if different descriptor or bank != 0
    if (desc_addr != last_desc || bank != 0) {
        std::cerr << "[SELMEM #" << selmem_count << "] desc=0x" << std::hex << desc_addr
                  << " [base=" << (int)d_base << " size=" << (int)d_size
                  << " attr=" << (int)d_attr << " bank=" << (int)bank << "]"
                  << " PC=0x" << cpu_->regs.PC.get_pair16() << std::dec << "\n";
        last_desc = desc_addr;
    }

    mem_->select_bank(bank);
    do_ret();
}

void XIOS::do_polldevice() {
    // C = device number to poll
    // Return 0xFF if ready, 0x00 if not
    // Device numbering per simh XIOS:
    //   Even devices (0,2,4,6) = console output 0-3
    //   Odd devices (1,3,5,7) = console input 0-3
    //   Console number = device / 2
    uint8_t device = cpu_->regs.BC.get_low();

    static int poll_count = 0;
    poll_count++;
    if (poll_count <= 50) {
        std::cerr << "[POLL #" << poll_count << "] device=" << (int)device << "\n";
    }

    uint8_t result = 0x00;

    if (device < 8) {
        int console = device / 2;
        bool is_input = (device & 1) != 0;

        if (is_input) {
            // Console input - check if character ready
            Console* con = ConsoleManager::instance().get(console);
            if (con && con->const_status()) {
                result = 0xFF;
            }
        } else {
            // Console output - always ready
            result = 0xFF;
        }
    }

    cpu_->regs.AF.set_high(result);
    do_ret();
}

void XIOS::do_startclock() {
    std::cerr << "[STARTCLOCK] Enabling tick interrupts\n";
    tick_enabled_.store(true);
    do_ret();
}

void XIOS::do_stopclock() {
    tick_enabled_.store(false);
    do_ret();
}

void XIOS::do_exitregion() {
    // Just trace - let the Z80 code handle EI based on its PREEMP variable
    static int exit_count = 0;
    exit_count++;
    if (exit_count <= 20) {
        std::cerr << "[EXITRGN #" << exit_count << "] IFF1_before=" << (int)cpu_->regs.IFF1 << "\n";
    }
    // Don't touch IFF here - let Z80's EI instruction handle it
    do_ret();
}

void XIOS::do_maxconsole() {
    // Return max console number (0-based, so 4 consoles = return 3)
    // Read from SYSTEM.DAT at offset 1 and subtract 1
    uint8_t num_consoles = mem_->read_common(0xFF01);  // Number of consoles
    uint8_t max_num = num_consoles > 0 ? num_consoles - 1 : 0;

    static bool traced = false;
    if (!traced) {
        traced = true;
        std::cerr << "[MAXCON] num_consoles=" << (int)num_consoles
                  << " returning " << (int)max_num << "\n";
    }

    cpu_->regs.AF.set_high(max_num);
    do_ret();
}

void XIOS::do_systeminit() {
    // C = breakpoint RST number
    // DE = breakpoint handler address
    // HL = XIOS direct jump table address

    static int init_count = 0;
    init_count++;
    std::cerr << "[SYSINIT #" << init_count << "] C=0x" << std::hex
              << (int)cpu_->regs.BC.get_low()
              << " DE=0x" << cpu_->regs.DE.get_pair16()
              << " HL=0x" << cpu_->regs.HL.get_pair16()
              << std::dec << "\n";

    // Initialize consoles
    ConsoleManager::instance().init();

    // Copy interrupt vector from current bank to all other banks
    // The Z80 code wrote JP INTHND to 0x0038-0x003A, but only in the current bank.
    // We need to replicate this to all banks so interrupts work regardless of bank.
    uint8_t current_bank = mem_->current_bank();
    uint8_t vec_0038 = mem_->fetch_mem(0x0038);
    uint8_t vec_0039 = mem_->fetch_mem(0x0039);
    uint8_t vec_003A = mem_->fetch_mem(0x003A);

    // Copy interrupt vector to all other banks so interrupts work regardless of bank
    for (int bank = 0; bank < 8; bank++) {
        if (bank != current_bank) {
            mem_->write_bank(bank, 0x0038, vec_0038);
            mem_->write_bank(bank, 0x0039, vec_0039);
            mem_->write_bank(bank, 0x003A, vec_003A);
        }
    }

    // IMPORTANT: Enable the clock NOW to allow timer-based preemption.
    //
    // The issue: CONBDOS console input (conin) uses a busy-loop that calls
    // XIOS CONIN repeatedly until a character is available. If TMP starts
    // running before STARTCLOCK is called, it will busy-loop forever waiting
    // for console input, preventing Init from creating other TMPs.
    //
    // By enabling the clock at SYSINIT, timer interrupts (60Hz) will preempt
    // the busy-looping TMP, giving Init time slices to complete initialization.
    //
    // The Z80 SYSINIT code does EI, so interrupts will be enabled at the CPU
    // level. We just need to ensure our timer thread delivers the interrupts.
    std::cerr << "[SYSINIT] Enabling clock for preemptive scheduling\n";
    tick_enabled_.store(true);

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

    static int swtuser_count = 0;
    swtuser_count++;
    if (swtuser_count <= 20) {
        uint8_t bank = (desc_addr != 0) ? mem_->fetch_mem(desc_addr + 3) : 0;
        std::cerr << "[SWTUSER #" << swtuser_count << "] desc=0x" << std::hex << desc_addr
                  << " bank=" << std::dec << (int)bank << "\n";
    }

    if (desc_addr != 0) {
        uint8_t bank = mem_->fetch_mem(desc_addr + 3);
        mem_->select_bank(bank);
    }
    do_ret();
}

void XIOS::do_swtsys() {
    // Switch to system bank (bank 0)
    static int swtsys_count = 0;
    swtsys_count++;
    if (swtsys_count <= 20) {
        std::cerr << "[SWTSYS #" << swtsys_count << "] from bank=" << (int)mem_->current_bank() << "\n";
    }
    mem_->select_bank(0);
    do_ret();
}

void XIOS::do_pdisp() {
    // Process dispatcher entry point - called at end of interrupt handler
    // The Z80 code does EI then JP PDISP, but EI's effect is delayed
    // Re-enable interrupts here to ensure they stay enabled
    static int pdisp_count = 0;
    pdisp_count++;
    if (pdisp_count <= 30) {
        std::cerr << "[PDISP #" << pdisp_count << "] PC=0x" << std::hex
                  << cpu_->regs.PC.get_pair16() << std::dec << "\n";
    }
    cpu_->regs.IFF1 = 1;
    cpu_->regs.IFF2 = 1;
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
