// z80_thread.cpp - Z80 CPU emulation thread implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "z80_thread.h"
#include "mpm_cpu.h"
#include "banked_mem.h"
#include "xios.h"
#include <fstream>
#include <cstring>
#include <iostream>
#include <iomanip>

extern bool g_debug_enabled;


Z80Thread::Z80Thread()
    : running_(false)
    , stop_requested_(false)
    , instruction_count_(0)
    , tick_count_(0)
{
}

Z80Thread::~Z80Thread() {
    stop();
}

bool Z80Thread::init(const std::string& boot_image) {
    // Create memory (5 banks for MP/M II with 4 user segments plus system)
    // Banks: 0=system, 1-4=user TMPs
    // With 5 banks: 5 * 48KB banked + 16KB common = 256KB total
    memory_ = std::make_unique<BankedMemory>(5);

    // Create CPU with memory (MpmCpu extends qkz80 with I/O port handling)
    cpu_ = std::make_unique<MpmCpu>(memory_.get());
    cpu_->set_cpu_mode(qkz80::MODE_Z80);

    // Create XIOS
    xios_ = std::make_unique<XIOS>(cpu_.get(), memory_.get());

    // Connect CPU to XIOS and banked memory for port dispatch
    cpu_->set_xios(xios_.get());
    cpu_->set_banked_mem(memory_.get());

    // Load boot image if provided
    if (!boot_image.empty()) {
        std::ifstream file(boot_image, std::ios::binary);
        if (!file) {
            return false;
        }

        // Get file size
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        // Read into buffer
        std::vector<uint8_t> buffer(size);
        file.read(reinterpret_cast<char*>(buffer.data()), size);

        // Boot image is a 64KB memory image created by mkboot
        // It contains: page zero setup, MPMLDR at 0x0100, XIOS at 0x8800
        // Load at 0x0000 (the image is already positioned correctly)
        memory_->load(0, 0x0000, buffer.data(), buffer.size());

        // Set PC to 0x0100 where MPMLDR starts
        cpu_->regs.PC.set_pair16(0x0100);

        // Set up stack pointer in high memory (will be reset by MPMLDR)
        cpu_->regs.SP.set_pair16(0x0080);

    }

    return true;
}

bool Z80Thread::load_mpm_sys(const std::string& mpm_sys_path) {
    // Direct MPM.SYS loader - bypasses MPMLDR
    // See docs/mpmldr_analysis.md for details on MPM.SYS format

    std::ifstream file(mpm_sys_path, std::ios::binary);
    if (!file) {
        std::cerr << "Cannot open MPM.SYS: " << mpm_sys_path << "\n";
        return false;
    }

    // Get file size
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Read first 256 bytes (SYSTEM.DAT - 2 records of 128 bytes)
    uint8_t sysdat[256];
    file.read(reinterpret_cast<char*>(sysdat), 256);
    if (!file) {
        std::cerr << "Failed to read SYSTEM.DAT from MPM.SYS\n";
        return false;
    }

    // Extract key fields from SYSTEM.DAT (see docs/mpmldr_analysis.md)
    uint8_t mem_top = sysdat[0];           // Top page of memory
    uint8_t nmb_cns = sysdat[1];           // Number of consoles
    uint8_t brkpt_rst = sysdat[2];         // Breakpoint RST number
    uint8_t bank_switched = sysdat[4];     // Bank switched system
    uint8_t z80_cpu = sysdat[5];           // Z80 CPU flag
    uint8_t xios_jmp_tbl_base = sysdat[7]; // XIOS jump table page
    uint8_t resbdos_base = sysdat[8];      // Resident BDOS page
    uint8_t xdos_base = sysdat[11];        // XDOS base page = entry point
    uint8_t rsp_base = sysdat[12];         // RSP base page
    uint8_t bnkxios_base = sysdat[13];     // BNKXIOS base page
    uint8_t bnkbdos_base = sysdat[14];     // BNKBDOS base page
    uint8_t nmb_mem_seg = sysdat[15];      // Number of memory segments
    uint16_t nmb_records = sysdat[120] | (sysdat[121] << 8);
    uint8_t ticks_per_sec = sysdat[122];   // Ticks per second
    uint8_t system_drive = sysdat[123];    // System drive (1=A)
    uint8_t common_base = sysdat[124];     // Common memory base page
    uint8_t bnkxdos_base = sysdat[242];    // Banked XDOS page
    uint8_t tmp_base = sysdat[247];        // TMP base page

    // Print MPMLDR-style signon
    std::cout << "\n\nMP/M II V2.1 Loader (Emulator Direct Load)\n";
    std::cout << "Copyright (C) 1981, Digital Research\n\n";

    // Print system configuration (like MPMLDR)
    std::cout << "Nmb of consoles     =  " << (int)nmb_cns << "\n";
    std::cout << "Breakpoint RST #    =  " << (int)brkpt_rst << "\n";
    if (z80_cpu) {
        std::cout << "Z80 Alternate register set saved/restored by dispatcher\n";
    }
    if (bank_switched) {
        std::cout << "Common base addr    =  " << std::hex << std::uppercase
                  << std::setw(4) << std::setfill('0') << (common_base * 256)
                  << "H\n" << std::dec;
    }
    std::cout << "Banked BDOS file manager\n";
    std::cout << "Nmb of ticks/second =  " << (int)ticks_per_sec << "\n";
    if (system_drive >= 1 && system_drive <= 16) {
        std::cout << "System drive        =  " << (char)('A' + system_drive - 1) << ":\n";
    } else {
        std::cout << "System drive        =  A: (default)\n";
    }

    // Print Memory Segment Table header
    std::cout << "Memory Segment Table:\n";

    // Helper lambda to print segment info (MPMLDR format: NAME BASE SIZE)
    auto print_segment = [](const char* name, uint16_t base, uint16_t size) {
        std::cout << std::setfill(' ') << std::left << std::setw(12) << name
                  << "  " << std::hex << std::uppercase << std::right
                  << std::setfill('0') << std::setw(4) << base << "H"
                  << "  " << std::setw(4) << size << "H"
                  << std::dec << std::setfill(' ') << "\n";
    };

    // Calculate and print segments (similar to MPMLDR display_OS)
    uint16_t sysdat_addr = mem_top * 256;
    print_segment("SYSTEM DAT", sysdat_addr, 0x0100);

    // TMPD size = ((nmb_cns-1)/4+1)*256
    uint16_t tmpd_size = ((nmb_cns - 1) / 4 + 1) * 256;
    uint16_t tmpd_addr = sysdat_addr - tmpd_size;
    print_segment("TMPD    DAT", tmpd_addr, tmpd_size);

    // XIOS jump table
    print_segment("XIOSJMP TBL", xios_jmp_tbl_base * 256, 0x0100);

    // RESBDOS
    print_segment("RESBDOS SPR", resbdos_base * 256,
                  (xios_jmp_tbl_base - resbdos_base) * 256);

    // XDOS
    print_segment("XDOS    SPR", xdos_base * 256,
                  (resbdos_base - xdos_base) * 256);

    // BNKXIOS
    print_segment("BNKXIOS SPR", bnkxios_base * 256,
                  (rsp_base - bnkxios_base) * 256);

    // BNKBDOS
    print_segment("BNKBDOS SPR", bnkbdos_base * 256,
                  (bnkxios_base - bnkbdos_base) * 256);

    // BNKXDOS
    print_segment("BNKXDOS SPR", bnkxdos_base * 256,
                  (bnkbdos_base - bnkxdos_base) * 256);

    // TMP
    print_segment("TMP     SPR", tmp_base * 256,
                  (bnkxdos_base - tmp_base) * 256);

    // Print memory map separator
    std::cout << "-------------------------\n";

    // Print memory segment entries (mem_seg_tbl at offset 16-47)
    std::cout << "MP/M II Sys";
    std::cout << "  " << std::hex << std::uppercase << std::right
              << std::setw(4) << std::setfill('0') << 0 << "H";
    std::cout << "  " << std::setw(4) << std::setfill('0')
              << (common_base * 256) << "H";
    if (bank_switched) {
        std::cout << "  Bank 0";
    }
    std::cout << std::dec << std::setfill(' ') << "\n";

    // User memory segments from mem_seg_tbl (8 entries, 4 bytes each at offset 16)
    for (int i = 0; i < nmb_mem_seg && i < 8; i++) {
        uint8_t seg_base = sysdat[16 + i * 4];
        uint8_t seg_size = sysdat[16 + i * 4 + 1];
        uint8_t seg_bank = sysdat[16 + i * 4 + 3];

        if (seg_size > 0) {
            std::cout << "Memseg  Usr";
            std::cout << "  " << std::hex << std::uppercase << std::right
                      << std::setw(4) << std::setfill('0') << (seg_base * 256) << "H";
            std::cout << "  " << std::setw(4) << std::setfill('0')
                      << (seg_size * 256) << "H";
            if (bank_switched) {
                std::cout << "  Bank " << std::dec << (int)seg_bank;
            }
            std::cout << std::dec << std::setfill(' ') << "\n";
        }
    }

    std::cout << "\n";

    // Validate file size (allow for CP/M record padding tolerance)
    if (nmb_records < 3) {
        std::cerr << "Invalid nmb_records: " << nmb_records << "\n";
        return false;
    }
    size_t expected_size = nmb_records * 128;
    // Allow up to 128 bytes short (CP/M doesn't always pad the last record)
    if (file_size + 128 < expected_size) {
        std::cerr << "MPM.SYS too small: " << file_size << " bytes, expected ~"
                  << expected_size << "\n";
        return false;
    }

    // Load remaining records (2 through nmb_records-1) DOWNWARD from mem_top
    // Records 0-1 = SYSTEM.DAT, Records 2+ = SPR files
    uint16_t load_addr = mem_top * 256;
    uint16_t records_loaded = 0;

    for (uint16_t rec = 2; rec < nmb_records; rec++) {
        load_addr -= 128;

        uint8_t record[128] = {0};  // Zero-fill in case of partial read
        file.read(reinterpret_cast<char*>(record), 128);
        size_t bytes_read = file.gcount();

        if (bytes_read == 0 && rec < nmb_records - 1) {
            // Unexpected EOF before last record
            std::cerr << "Warning: EOF at record " << rec << " (expected " << nmb_records << ")\n";
            break;
        }

        // Load into bank 0 (system bank) - addresses >= COMMON_BASE go to common area
        memory_->load(0, load_addr, record, 128);
        records_loaded++;

        if (file.eof()) break;
    }

    // Copy SYSTEM.DAT to mem_top * 256 (where MP/M expects it)
    memory_->load(0, sysdat_addr, sysdat, 256);

    // Set entry point: xdos_base * 256
    uint16_t entry_point = xdos_base * 256;
    cpu_->regs.PC.set_pair16(entry_point);

    // Set stack pointer - MPMLDR sets it to just below the entry point data
    cpu_->regs.SP.set_pair16(sysdat_addr);

    // Update XIOS base to match BNKXIOS location
    xios_->set_base(bnkxios_base * 256);

    std::cout << "Loaded " << records_loaded << " records ("
              << (records_loaded * 128) << " bytes)\n";
    std::cout << "Entry point: " << std::hex << std::uppercase
              << std::setw(4) << std::setfill('0') << entry_point << "H\n"
              << std::dec << std::setfill(' ');

    // Initialize address 5 in all banks with JMP to XDOS
    uint16_t xdos_entry = xdos_base * 256;
    for (int bank = 0; bank < 4; bank++) {
        memory_->write_bank(bank, 0x0005, 0xC3);  // JP opcode
        memory_->write_bank(bank, 0x0006, xdos_entry & 0xFF);
        memory_->write_bank(bank, 0x0007, (xdos_entry >> 8) & 0xFF);
    }

    // NOTE: The COMMONBASE entries (PDISP at +9, XDOS at +12) are already
    // correctly patched in the loaded MPM.SYS file by GENSYS. The XDOS entry
    // at C757 should jump to E79C (the real XDOS BDOS entry), NOT to CE00.
    // DO NOT overwrite these - they are correct as loaded!

    // Debug: show COMMONBASE entries after loading (only with DEBUG=1)
    if (g_debug_enabled) {
        uint16_t commonbase = bnkxios_base * 256 + 0x4B;
        std::cerr << "COMMONBASE at " << std::hex << commonbase << ":\n";
        for (int i = 0; i < 18; i += 3) {
            uint8_t op = memory_->read_common(commonbase + i);
            uint16_t addr = memory_->read_common(commonbase + i + 1) |
                           (memory_->read_common(commonbase + i + 2) << 8);
            const char* names[] = {"COLDBOOT", "SWTUSER", "SWTSYS", "PDISP", "XDOS", "SYSDAT"};
            std::cerr << "  " << names[i/3] << ": ";
            if (op == 0xC3) {
                std::cerr << "JP " << std::setw(4) << std::setfill('0') << addr << "H\n";
            } else {
                std::cerr << "?? (" << (int)op << ")\n";
            }
        }
        std::cerr << std::dec << std::setfill(' ');
    }

    return true;
}

void Z80Thread::start() {
    if (running_.load()) return;

    stop_requested_.store(false);
    timed_out_.store(false);
    running_.store(true);
    start_time_ = std::chrono::steady_clock::now();
    next_tick_ = start_time_;
    tick_count_ = 0;
    instruction_count_.store(0);

    thread_ = std::thread(&Z80Thread::thread_func, this);
}

void Z80Thread::stop() {
    if (!running_.load()) return;

    stop_requested_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
}

void Z80Thread::set_xios_base(uint16_t base) {
    if (xios_) {
        xios_->set_base(base);
    }
}

void Z80Thread::enable_interrupts(bool enable) {
    if (cpu_) {
        cpu_->regs.IFF1 = enable ? 1 : 0;
        cpu_->regs.IFF2 = enable ? 1 : 0;
    }
}

uint64_t Z80Thread::cycles() const {
    return cpu_ ? cpu_->cycles : 0;
}

void Z80Thread::thread_func() {
    while (!stop_requested_.load()) {
        // Check for timeout
        auto now = std::chrono::steady_clock::now();
        if (timeout_seconds_ > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
            if (elapsed >= timeout_seconds_) {
                std::cerr << "\n[TIMEOUT] Boot timed out after " << elapsed << " seconds\n";
                std::cerr << "[TIMEOUT] PC=0x" << std::hex << cpu_->regs.PC.get_pair16()
                          << " SP=0x" << cpu_->regs.SP.get_pair16()
                          << " bank=" << std::dec << (int)memory_->current_bank()
                          << " instructions=" << instruction_count_.load() << "\n";
                timed_out_.store(true);
                stop_requested_.store(true);
                break;
            }
        }

        // Check for timer interrupt (60Hz tick)
        if (now >= next_tick_) {
            next_tick_ += TICK_INTERVAL;

            // Request tick interrupt if clock is enabled
            if (xios_->clock_enabled()) {
                static int tick_count_local = 0;
                tick_count_local++;
                // Skip first few ticks to let boot complete initial setup
                if (tick_count_local < 10) continue;
                // Log first 30 ticks and then every 60 (only in debug mode)
                if (g_debug_enabled && (tick_count_local <= 30 || (tick_count_local % 60) == 0)) {
                    std::cerr << "[TICK] #" << tick_count_local
                              << " IFF=" << (int)cpu_->regs.IFF1
                              << " bank=" << (int)memory_->current_bank()
                              << " PC=0x" << std::hex << cpu_->regs.PC.get_pair16() << std::dec << "\n";
                }
                // Check IFF state and request interrupt if enabled
                // Z80 hardware automatically disables interrupts (IFF=0) when
                // an interrupt is acknowledged. The XDOS dispatcher should
                // re-enable them with EI before resuming processes.
                //
                // If IFF stays 0 after first interrupt, XDOS isn't EI'ing properly
                // In that case, we need to force-enable to keep the system running.
                static int iff_zero_count = 0;
                if (cpu_->regs.IFF1) {
                    iff_zero_count = 0;  // Reset counter
                    cpu_->request_rst(7);  // RST 38H
                } else {
                    iff_zero_count++;
                    // If IFF has been 0 for too many ticks, force-enable
                    // This works around XDOS not EI'ing when returning to process
                    if (iff_zero_count >= 5 && tick_count_local > 15) {
                        if (g_debug_enabled && iff_zero_count == 5) {
                            std::cerr << "[TICK] Forcing IFF=1 after " << iff_zero_count
                                      << " ticks with IFF=0, PC=0x" << std::hex
                                      << cpu_->regs.PC.get_pair16() << std::dec << "\n";
                        }
                        cpu_->regs.IFF1 = 1;
                        cpu_->regs.IFF2 = 1;
                        cpu_->request_rst(7);
                    }
                }
                xios_->tick();
            }

            // Check for one-second tick
            if (++tick_count_ >= 60) {
                tick_count_ = 0;
                xios_->one_second_tick();

                // Dump TMPD area once after 2 seconds (only in debug mode)
                static int sec_count = 0;
                sec_count++;
                if (g_debug_enabled && sec_count == 2) {
                    std::cerr << "[TICK] TMPD dump after 2 seconds:\n";
                    for (int tmp = 0; tmp < 4; tmp++) {
                        int base = 0xFE00 + (tmp * 64);
                        std::cerr << "  TMP" << tmp << " at 0x" << std::hex << base << ": ";
                        for (int i = 0; i < 16; i++) {
                            std::cerr << std::setw(2) << std::setfill('0')
                                      << (int)memory_->read_common(base + i) << " ";
                        }
                        std::cerr << "\n";
                    }
                    // Also dump process descriptors by looking for active ones
                    std::cerr << "[TICK] Looking for active process descriptors E900-EA00:\n";
                    for (int pd = 0; pd < 16; pd++) {
                        int addr = 0xE900 + pd * 16;
                        // Check if this looks like a valid PD (non-zero link or status)
                        uint8_t first = memory_->read_common(addr);
                        uint8_t second = memory_->read_common(addr + 1);
                        if (first != 0 || second != 0) {
                            std::cerr << "  0x" << std::hex << addr << ": ";
                            for (int i = 0; i < 16; i++) {
                                std::cerr << std::setw(2) << std::setfill('0')
                                          << (int)memory_->read_common(addr + i) << " ";
                            }
                            std::cerr << "\n";
                        }
                    }
                    std::cerr << std::dec;
                }
            }
        }

        // Handle HALT - CPU waits for interrupt
        if (cpu_->is_halted()) {
            // If clock is running and interrupts pending, deliver and wake
            if (cpu_->check_interrupts()) {
                cpu_->clear_halted();
            } else {
                // Sleep briefly to avoid busy-waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
        }

        // Deliver any pending interrupts before executing
        uint16_t pc_before = cpu_->regs.PC.get_pair16();
        bool delivered = cpu_->check_interrupts();
        uint16_t pc_after = cpu_->regs.PC.get_pair16();

        // Log when interrupt is actually delivered (only in debug mode)
        if (g_debug_enabled && delivered) {
            static int int_count = 0;
            int_count++;
            if (int_count <= 10) {
                std::cerr << "[INT] #" << int_count
                          << " delivered, PC: 0x" << std::hex << pc_before
                          << " -> 0x" << pc_after << std::dec << "\n";
            }
        }

        // Execute one instruction
        cpu_->execute();
        instruction_count_++;

        // Trace XDOS calls (only in debug mode)
        static bool traced_creates = false;
        if (g_debug_enabled && !traced_creates) {
            uint16_t pc = cpu_->regs.PC.get_pair16();
            // Check if PC is at XDOS entry point
            if (pc == 0xCE00 || pc == 0xCE06) {
                uint8_t func = cpu_->regs.BC.get_low();
                // XDOS function 144 (0x90) = CREATE PROCESS
                if (func == 0x90) {
                    uint16_t pd_addr = cpu_->regs.DE.get_pair16();
                    // Read process name from PD offset 6-13
                    std::cerr << "[XDOS CREATE] PD=0x" << std::hex << pd_addr
                              << " name='";
                    for (int i = 6; i < 14; i++) {
                        uint8_t ch = memory_->read_common(pd_addr + i) & 0x7F;
                        if (ch >= 0x20 && ch < 0x7F) {
                            std::cerr << (char)ch;
                        }
                    }
                    std::cerr << "'" << std::dec << "\n";
                }
                // XDOS function 143 (0x8F) = TERMINATE
                if (func == 0x8F) {
                    std::cerr << "[XDOS TERMINATE] E=" << (int)cpu_->regs.DE.get_low() << "\n";
                    traced_creates = true;  // Stop tracing after init terminates
                }
            }
        }
    }
}
