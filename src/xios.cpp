// xios.cpp - MP/M II Extended I/O System implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "xios.h"
#include "console.h"
#include "banked_mem.h"
#include "disk.h"
#include "sftp_bridge.h"
#include "qkz80.h"
#include <iostream>
#include <iomanip>
#include <set>

// Debug output control - set to true to enable verbose debug output
static constexpr bool DEBUG_BOOT = false;
static constexpr bool DEBUG_DISK = false;  // Disk read tracing
static constexpr bool DEBUG_DISK_ERRORS = true;
static constexpr bool DEBUG_XIOS = false;

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
    , systeminit_done_(false)
{
}

// Global flag set when "Bank 7" is displayed
bool g_boot_display_complete = false;

void XIOS::handle_port_dispatch(uint8_t func) {
    static int call_count = 0;
    static int const_count = 0;
    static int post_boot_xios_calls = 0;
    call_count++;

    // Trace XIOS calls after SYSTEMINIT (disabled by setting to 0)
    if (DEBUG_XIOS && systeminit_done_.load() && post_boot_xios_calls < 100) {
        post_boot_xios_calls++;
        uint16_t pc = cpu_->regs.PC.get_pair16();
        uint16_t sp = cpu_->regs.SP.get_pair16();
        uint8_t bank = mem_->current_bank();

        // Read return address from stack
        uint8_t ret_lo = mem_->read_bank(bank, sp);
        uint8_t ret_hi = mem_->read_bank(bank, sp + 1);
        uint16_t ret_addr = ret_lo | (ret_hi << 8);

        std::cerr << "[XIOS] f=" << std::hex << (int)func
                  << " PC=" << pc << " SP=" << sp
                  << " ret=" << ret_addr
                  << " bk=" << std::dec << (int)bank << "\n";
    }

    // After boot display is complete, trace XIOS calls
    if (DEBUG_XIOS && g_boot_display_complete) {
        post_boot_xios_calls++;
        // Log first 50 non-CONST/CONOUT calls after boot
        if (func != XIOS_CONST && func != XIOS_CONOUT && post_boot_xios_calls <= 100) {
            uint16_t pc = cpu_->regs.PC.get_pair16();
            std::cerr << "[POST-BOOT XIOS #" << post_boot_xios_calls << "] func=0x"
                      << std::hex << (int)func << " PC=0x" << pc << std::dec << std::endl;
        }
        // Log significant CONST polling after boot
        if (func == XIOS_CONST && (const_count == 10000 || const_count == 100000 || const_count == 1000000)) {
            uint16_t pc = cpu_->regs.PC.get_pair16();
            std::cerr << "[POST-BOOT CONST #" << const_count << "] PC=0x"
                      << std::hex << pc << std::dec << std::endl;
        }
    }

    // Track repeated CONST calls (polling) - reduced logging
    if (func == XIOS_CONST) {
        const_count++;
        // Only log significant milestones
        if (DEBUG_XIOS && (const_count == 100000 || const_count == 1000000)) {
            uint16_t pc = cpu_->regs.PC.get_pair16();
            std::cerr << "[CONST poll #" << const_count << "] PC=0x"
                      << std::hex << pc << std::dec << std::endl;
        }
    } else {
        const_count = 0;
    }

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

        // SFTP bridge entries
        case XIOS_SFTP_POLL:  do_sftp_poll(); break;
        case XIOS_SFTP_GET:   do_sftp_get(); break;
        case XIOS_SFTP_PUT:   do_sftp_put(); break;
        case XIOS_SFTP_HELLO: do_sftp_hello(); break;
        case XIOS_SFTP_ENTRY: do_sftp_entry(); break;
        case XIOS_SFTP_JMPADDR: do_sftp_jmpaddr(); break;
        case XIOS_SFTP_EPVAL: do_sftp_epval(); break;
        case XIOS_SFTP_DEBUG: do_sftp_debug(); break;
        case XIOS_SFTP_RSPBASE: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[RSP] RSPBASE = 0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case XIOS_SFTP_BDOSENT: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[RSP] bdos$entry = 0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x74: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[BDOS] arg1 (BC) = 0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x75: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[BDOS] arg2 (DE) = 0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x76: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[BDOS] caller PC = 0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x77: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[RSP] SP = 0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x78: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[RSP] ret_addr = 0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x79: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[BDOS] RSPBASE = 0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x7a: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[BDOS] bdos_entry = 0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x7b: {
            uint8_t val = cpu_->regs.BC.get_low();
            std::cerr << "[BDOS] (SP+3) = 0x" << std::hex << (int)val << std::dec << "\n";
            break;
        }
        case 0x7c: {
            uint8_t val = cpu_->regs.BC.get_low();
            std::cerr << "[BDOS] (SP+4) = 0x" << std::hex << (int)val << std::dec << "\n";
            break;
        }
        case 0x7d: {
            uint8_t val = cpu_->regs.BC.get_low();
            std::cerr << "[BDOS] (SP+5) = 0x" << std::hex << (int)val << std::dec << "\n";
            break;
        }
        case 0x7e: {
            uint8_t val = cpu_->regs.BC.get_low();
            std::cerr << "[BDOS] func = 0x" << std::hex << (int)val << std::dec << "\n";
            // Save func for FCB dump in 0x7f handler
            last_bdos_func_ = val;
            break;
        }
        case 0x7f: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[BDOS] parm = 0x" << std::hex << val << std::dec << "\n";
            // If this is search first (0x11) or search next (0x12), dump FCB
            if (last_bdos_func_ == 0x11 || last_bdos_func_ == 0x12) {
                std::cerr << "[BDOS] FCB at 0x" << std::hex << val << ": drv=";
                uint8_t b = mem_->read_bank(0, val);
                std::cerr << (int)b << " name=";
                for (int i = 1; i < 12; i++) {
                    b = mem_->read_bank(0, val + i);
                    char c = (b & 0x7f);
                    if (c >= 32 && c < 127) std::cerr << c;
                    else std::cerr << ".";
                }
                std::cerr << " hex=";
                for (int i = 1; i < 12; i++) {
                    b = mem_->read_bank(0, val + i);
                    std::cerr << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
                }
                std::cerr << std::dec << "\n";
                // Also dump buffer at 0x95a7 for comparison
                std::cerr << "[BDOS] SFTPBUF at 0x95a7[4..14]=";
                for (int i = 4; i < 15; i++) {
                    b = mem_->read_bank(0, 0x95a7 + i);
                    std::cerr << std::hex << (int)b << " ";
                }
                std::cerr << std::dec << "\n";
            }
            break;
        }

        // GETBUFBYTE debug trace
        case 0x80: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[GETBUFBYTE] buf_base=0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x81: {
            uint16_t val = cpu_->regs.BC.get_pair16();
            std::cerr << "[GETBUFBYTE] final_addr=0x" << std::hex << val << std::dec << "\n";
            break;
        }
        case 0x82: {
            uint8_t val = cpu_->regs.BC.get_low();
            std::cerr << "[GETBUFBYTE] value=0x" << std::hex << (int)val << std::dec << "\n";
            break;
        }

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
    if (console >= 8) {
      uint16_t pc = cpu_->regs.PC.get_pair16();
      std::cerr << "[CONST] Invalid console " << (int)console
                << " DE=0x" << std::hex << cpu_->regs.DE.get_pair16()
                << " PC=0x" << pc << std::dec << "\n";
      throw std::invalid_argument("invalid console status");
    }

    Console* con = ConsoleManager::instance().get(console);
    uint8_t status = con ? con->const_status() : 0x00;

    // DEBUG: Track LDRBIOS polling during boot
    if (DEBUG_BOOT) {
        static int boot_const_count = 0;
        static bool boot_phase_complete = false;
        uint16_t pc = cpu_->regs.PC.get_pair16();
        if (pc >= 0x1700 && pc < 0x1900) {  // LDRBIOS range
            boot_const_count++;
            if (boot_const_count == 10000) {
                std::cerr << "[DEBUG] LDRBIOS polled CONST 10000 times" << std::endl;
            }
            if (boot_const_count == 100000) {
                std::cerr << "[DEBUG] LDRBIOS polled CONST 100000 times - trying keypress injection" << std::endl;
                status = 0xFF;  // Pretend character available
            }
        } else if (!boot_phase_complete && boot_const_count > 0) {
            std::cerr << "[DEBUG] Boot phase ended after " << boot_const_count
                      << " LDRBIOS CONST polls, PC now at 0x" << std::hex << pc << std::dec << std::endl;
            boot_phase_complete = true;
        }
    }

    cpu_->regs.AF.set_high(status);
}

void XIOS::do_conin() {
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    if (console >= 8) {
      uint16_t pc = cpu_->regs.PC.get_pair16();
      std::cerr << "[CONIN] Invalid console " << (int)console
                << " DE=0x" << std::hex << cpu_->regs.DE.get_pair16()
                << " PC=0x" << pc << std::dec << "\n";
      throw std::invalid_argument("invalid console conin");
    } 

    Console* con = ConsoleManager::instance().get(console);
    uint8_t ch = con ? con->read_char() : 0x1A;  // EOF default if no console

    cpu_->regs.AF.set_high(ch);
}

void XIOS::do_conout() {
    // D = console number (MP/M II XIOS convention), C = character
    uint8_t console = cpu_->regs.DE.get_high();  // D = console number
    uint8_t ch = cpu_->regs.BC.get_low();  // C = character

    // Track boot completion for flags (always needed)
    static bool boot_complete = false;
    if (console == 0) {
        static std::string boot_line;
        if (ch >= 0x20 && ch < 0x7F) {
            boot_line += (char)ch;
        } else if (ch == 0x0D || ch == 0x0A) {
            if (!boot_line.empty()) {
                if (DEBUG_BOOT) {
                    std::cerr << "[BOOT] " << boot_line << std::endl;
                }
                // Check if this is the last bank message
                if (boot_line.find("Bank 7") != std::string::npos) {
                    boot_complete = true;
                    g_boot_display_complete = true;  // Set global flag for XIOS tracing
                    if (DEBUG_BOOT) {
                        std::cerr << "[DEBUG] *** MPMLDR boot display complete ***" << std::endl;
                    }
                }
                boot_line.clear();
            }
        }
    }
    // Post-boot console output - just log first few per console (debug only)
    if (DEBUG_BOOT && boot_complete) {
        static int conout_count[8] = {0};
        if (console < 8) {
            conout_count[console]++;
            // Log first 100 chars per console
            if (conout_count[console] <= 100 && ch >= 0x20 && ch < 0x7F) {
                std::cerr << "[CON" << (int)console << "] " << (char)ch;
                if (ch == '\r' || ch == '\n') std::cerr << std::endl;
            }
        }
    }

    if (console >= 8) {
      uint16_t pc = cpu_->regs.PC.get_pair16();
      std::cerr << "[CONOUT] Invalid console " << (int)console
                << " DE=0x" << std::hex << cpu_->regs.DE.get_pair16()
                << " PC=0x" << pc << std::dec << "\n";
      throw std::invalid_argument("invalid console conout");
    }

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
    if (current_track_ > 1000) {
        std::cerr << "[SETTRK] T" << current_track_ << " (invalid for hd1k!)\n";
    }
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

    if (DEBUG_DISK_ERRORS && result != 0) {
        std::cerr << "[READ ERROR] T" << current_track_ << " S" << current_sector_
                  << " DMA=" << std::hex << dma_addr_ << std::dec
                  << " ERR=" << result << "\n";
    }

    if (DEBUG_DISK) {
        static int read_count = 0;
        static int high_track_count = 0;
        bool should_print = false;

        if (read_count < 300) {  // Show first 300 reads
            should_print = true;
        } else if (current_track_ > 100 && high_track_count < 250) {
            should_print = true;
            high_track_count++;
        }

        if (should_print) {
            std::cerr << "[READ] T" << current_track_ << " S" << current_sector_
                      << " -> " << std::hex << dma_addr_ << std::dec;
            if (result != 0) {
                std::cerr << " ERR=" << result;
            }
            std::cerr << "\n";
            read_count++;
        }
    }

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

    // Debug: trace bank switches after SYSTEMINIT
    if (systeminit_done_.load()) {
        if (DEBUG_XIOS) {
            static int selmem_count = 0;
            selmem_count++;
            if (selmem_count < 10) {
                uint16_t pc = cpu_->regs.PC.get_pair16();
                uint16_t sp = cpu_->regs.SP.get_pair16();
                uint8_t old_bank = mem_->current_bank();
                std::cerr << "[SELMEM] #" << selmem_count << " PC=" << std::hex << pc
                          << " SP=" << sp << " bank " << std::dec << (int)old_bank
                          << "->" << (int)bank << "\n";
            }
        }
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
    if (device > 15) {
      // Invalid device - just return "not ready" instead of crashing
      // MP/M may poll non-existent devices
      cpu_->regs.AF.set_high(0x00);
      return;
    }

    int console = device / 2;
    bool is_input = (device & 1) != 0; 

    Console* con = ConsoleManager::instance().get(console);
    if (is_input) {
      uint8_t status = con ? con->const_status() : 0;
      if (status) {
	result = 0xFF;
      }
    } else {
      // Console output - ready if queue has space
      bool full = con && con->output_queue().full();
      if (!full) {
        result = 0xFF;
      }
      // Queue full is normal when no SSH client is connected - don't log it
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
    if (DEBUG_XIOS) {
        std::cerr << "[XIOS] SYSTEMINIT BNK_VERSION=" << (int)bnk_version << "\n";
        std::cerr << "[XIOS] SYSTEMINIT called, IFF1=" << (int)cpu_->regs.IFF1 << "\n";
    }

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
    if (DEBUG_XIOS) {
        std::cerr << "[XIOS] Copied page 0 vectors to " << (num_banks - 1) << " banks\n";
    }

    // Initialize consoles
    ConsoleManager::instance().init();

    // Enable timer interrupts
    tick_enabled_.store(true);
    systeminit_done_.store(true);

    if (DEBUG_XIOS) {
        // Debug: show return address and stack
        uint16_t sp = cpu_->regs.SP.get_pair16();
        uint8_t bank = mem_->current_bank();
        uint16_t ret_lo = mem_->read_bank(bank, sp);
        uint16_t ret_hi = mem_->read_bank(bank, sp + 1);
        uint16_t ret_addr = ret_lo | (ret_hi << 8);
        std::cerr << "[XIOS] SYSTEMINIT returning: SP=" << std::hex << sp
                  << " RetAddr=" << ret_addr << " Bank=" << std::dec << (int)bank << "\n";

        // Debug: show RST 1 vector (at address 0x0008) in bank 0
        uint8_t rst1_opcode = mem_->read_bank(0, 0x0008);
        uint8_t rst1_lo = mem_->read_bank(0, 0x0009);
        uint8_t rst1_hi = mem_->read_bank(0, 0x000A);
        uint16_t rst1_target = rst1_lo | (rst1_hi << 8);
        std::cerr << "[XIOS] Bank 0 RST 1 vector: " << std::hex
                  << (int)rst1_opcode << " " << rst1_target << std::dec << "\n";
    }
}

void XIOS::do_idle() {
    // Idle procedure - called when no process is ready to run
    // The Z80 code does EI; HALT after this returns
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

// SFTP bridge handlers
// These interface with SftpBridge to exchange requests/replies with Z80 RSP

void XIOS::do_sftp_poll() {
    // Return 0xFF if SFTP request pending, 0x00 if idle
    bool pending = SftpBridge::instance().has_pending_request();
    static int poll_count = 0;
    poll_count++;
    // Log every 100 polls, or always if pending
    if (pending || (poll_count % 100) == 1) {
        std::cerr << "[XIOS] sftp_poll #" << poll_count << ": pending=" << pending << "\n";
    }
    cpu_->regs.AF.set_high(pending ? 0xFF : 0x00);
}

void XIOS::do_sftp_get() {
    // Copy SFTP request to Z80 buffer in bank 0
    // BC = address of buffer (in BRS module, bank 0)
    uint16_t buf_addr = cpu_->regs.BC.get_pair16();

    // Get request from bridge
    uint8_t buf[SFTP_BUF_SIZE];
    if (!SftpBridge::instance().get_request(buf, sizeof(buf))) {
        cpu_->regs.AF.set_high(0xFF);  // No request available
        return;
    }

    // Debug: show request being sent
    std::cerr << "[XIOS] sftp_get: buf_addr=0x" << std::hex << buf_addr << std::dec
              << " type=" << (int)buf[0]
              << " drive=" << (int)buf[1]
              << " user=" << (int)buf[2]
              << " flags=" << (int)buf[3]
              << " filename=";
    for (int i = 4; i < 12; i++) std::cerr << (char)(buf[i] >= 32 ? buf[i] : '.');
    std::cerr << ".";
    for (int i = 12; i < 15; i++) std::cerr << (char)(buf[i] >= 32 ? buf[i] : '.');
    std::cerr << " hex[4..14]=";
    for (int i = 4; i < 15; i++) std::cerr << std::hex << (int)buf[i] << " ";
    std::cerr << std::dec << "\n";

    // Write to Z80 bank 0 memory
    for (size_t i = 0; i < SFTP_BUF_SIZE; i++) {
        mem_->write_bank(0, buf_addr + i, buf[i]);
    }

    // Verify data was written correctly
    std::cerr << "[XIOS] verify: ";
    for (int i = 4; i < 15; i++) {
        uint8_t read_back = mem_->read_bank(0, buf_addr + i);
        std::cerr << std::hex << (int)read_back << " ";
    }
    std::cerr << std::dec << "\n";

    cpu_->regs.AF.set_high(0x00);  // Success
}

void XIOS::do_sftp_put() {
    // Read SFTP reply from Z80 buffer in bank 0
    // BC = address of buffer (in BRS module, bank 0)
    uint16_t buf_addr = cpu_->regs.BC.get_pair16();

    // Read from bank 0 memory
    uint8_t buf[SFTP_BUF_SIZE];
    for (size_t i = 0; i < SFTP_BUF_SIZE; i++) {
        buf[i] = mem_->read_bank(0, buf_addr + i);
    }

    std::cerr << "[XIOS] sftp_put: buf_addr=0x" << std::hex << buf_addr
              << " raw[0..5]=" << std::dec << (int)buf[0] << "," << (int)buf[1]
              << "," << (int)buf[2] << "," << (int)buf[3]
              << "," << (int)buf[4] << "," << (int)buf[5] << "\n";

    SftpBridge::instance().set_reply(buf, sizeof(buf));
    cpu_->regs.AF.set_high(0x00);  // Success
}

void XIOS::do_sftp_hello() {
    // RSP startup notification
    uint16_t pc = cpu_->regs.PC.get_pair16();
    std::cout << "SFTP RSP started (called from PC=0x" << std::hex << pc << std::dec << ")\n";
    cpu_->regs.AF.set_high(0x00);
}

void XIOS::do_sftp_entry() {
    // BRS entry point reached (debug)
    std::cerr << "[XIOS] SFTP BRS entry point reached\n";
    cpu_->regs.AF.set_high(0x00);
}

void XIOS::do_sftp_jmpaddr() {
    // Debug: report computed jump address (BC = address)
    uint16_t addr = cpu_->regs.BC.get_pair16();
    std::cerr << "[XIOS] SFTP computed jump addr: 0x" << std::hex << addr << std::dec << "\n";
    cpu_->regs.AF.set_high(0x00);
}

void XIOS::do_sftp_epval() {
    // Debug: report ENTRY_POINT value (BC = address)
    uint16_t addr = cpu_->regs.BC.get_pair16();
    std::cerr << "[XIOS] SFTP ENTRY_POINT value: 0x" << std::hex << addr << std::dec << "\n";
    cpu_->regs.AF.set_high(0x00);
}

void XIOS::do_sftp_debug() {
    // Generic debug trace: C = trace code (identifies location in RSP)
    uint8_t code = cpu_->regs.BC.get_low();
    uint16_t pc = cpu_->regs.PC.get_pair16();
    std::cerr << "[RSP] Trace #" << (int)code << " at PC=0x" << std::hex << pc << std::dec << "\n";
    cpu_->regs.AF.set_high(0x00);
}

