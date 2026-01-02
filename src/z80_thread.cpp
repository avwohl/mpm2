// z80_thread.cpp - Z80 CPU emulation thread implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "z80_thread.h"
#include "mpm_cpu.h"
#include "banked_mem.h"
#include "xios.h"
#include "console.h"
#include <fstream>
#include <cstring>
#include <iostream>
#include <set>
#include <iomanip>

extern bool g_debug_enabled;

// Global PC tracker for debugging memory writes
uint16_t g_debug_last_pc = 0;


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
    // Create memory (8 banks for MP/M II with 7 user segments plus system)
    // Banks: 0=system, 1-7=user TMPs
    // MP/M II supports max 7 user memory segments
    // With 8 banks: 8 * 48KB banked + 16KB common = 400KB total
    memory_ = std::make_unique<BankedMemory>(8);

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

    // Store in member variables for later use
    num_consoles_ = nmb_cns;
    num_mem_segs_ = nmb_mem_seg;

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

    // NOTE: Address 5 (BDOS entry) is set up by CLI.ASM when loading user programs.
    // We don't need to patch it here - MP/M's process initialization handles it.

    // NOTE: The COMMONBASE entries (PDISP at +9, XDOS at +12) are already
    // correctly patched in the loaded MPM.SYS file by GENSYS. The XDOS entry
    // at C757 should jump to E79C (the real XDOS BDOS entry), NOT to CE00.
    // DO NOT overwrite these - they are correct as loaded!

    // Set up RST 38H interrupt vector to call BNKXIOS SYSINIT entry point
    // SYSINIT is at offset 0x45 in the XIOS jump table
    // When called, DO_SYSINIT writes JP INTHND to 0x0038, sets IM1, enables EI
    // We'll execute SYSINIT before starting the main Z80 code
    uint16_t sysinit_addr = bnkxios_base * 256 + 0x45;

    // Save CPU state, call SYSINIT, restore
    // Push return address (we'll set PC to entry_point after)
    cpu_->regs.SP.set_pair16(entry_point);  // Use entry_point as temp stack
    memory_->write_bank(0, entry_point - 2, 0x00);  // Return address low
    memory_->write_bank(0, entry_point - 1, 0x00);  // Return address high
    cpu_->regs.SP.set_pair16(entry_point - 2);

    // Execute SYSINIT
    cpu_->regs.PC.set_pair16(sysinit_addr);
    std::cerr << "[BOOT] Executing SYSINIT at 0x" << std::hex << sysinit_addr << std::dec << "\n";
    for (int i = 0; i < 100; i++) {  // Run up to 100 instructions
        g_debug_last_pc = cpu_->regs.PC.get_pair16();
        cpu_->execute();
        if (cpu_->regs.PC.get_pair16() == 0x0000) {
            std::cerr << "[BOOT] SYSINIT completed\n";
            break;
        }
    }

    // Set PC to entry point for main execution
    // (SYSINIT returned to 0x0000, we need to start at the real entry point)
    cpu_->regs.PC.set_pair16(entry_point);
    cpu_->regs.SP.set_pair16(sysdat_addr);  // Reset SP to a sensible value

    // Verify RST 38H is set up
    uint8_t rst38_op = memory_->read_bank(0, 0x0038);
    uint16_t rst38_addr = memory_->read_bank(0, 0x0039) | (memory_->read_bank(0, 0x003A) << 8);
    std::cerr << "[BOOT] RST 38H: " << (rst38_op == 0xC3 ? "JP" : "??")
              << " 0x" << std::hex << rst38_addr << std::dec << "\n";
    std::cerr << "[BOOT] Starting at PC=0x" << std::hex << entry_point << std::dec << "\n";

    // Debug: show COMMONBASE entries after loading (only with DEBUG=1)
    if (g_debug_enabled) {
        uint16_t commonbase = bnkxios_base * 256 + 0x4B;
        std::cerr << "COMMONBASE at " << std::hex << commonbase << ":\n";
        // First 5 entries are JP instructions (3 bytes each)
        for (int i = 0; i < 15; i += 3) {
            uint8_t op = memory_->read_common(commonbase + i);
            uint16_t addr = memory_->read_common(commonbase + i + 1) |
                           (memory_->read_common(commonbase + i + 2) << 8);
            const char* names[] = {"COLDBOOT", "SWTUSER", "SWTSYS", "PDISP", "XDOS"};
            std::cerr << "  " << names[i/3] << ": ";
            if (op == 0xC3) {
                std::cerr << "JP " << std::setw(4) << std::setfill('0') << addr << "H\n";
            } else {
                std::cerr << "?? (" << (int)op << ")\n";
            }
        }
        // SYSDAT is a DW (2 bytes), not a JP instruction
        uint16_t sysdat_addr = memory_->read_common(commonbase + 15) |
                              (memory_->read_common(commonbase + 16) << 8);
        std::cerr << "  SYSDAT: " << std::setw(4) << std::setfill('0') << sysdat_addr << "H\n";

        // Verify SYSDAT content - byte 1 should be number of consoles
        if (sysdat_addr != 0) {
            uint8_t nmb_cns = memory_->read_common(sysdat_addr + 1);
            std::cerr << "  SYSDAT[1] (nmb consoles): " << std::dec << (int)nmb_cns << "\n";
        }
        std::cerr << std::dec << std::setfill(' ');

        // Dump XIOSJMP table at 0xFA00 (first few entries)
        std::cerr << "XIOSJMP table at FA00H:\n";
        for (int i = 0; i < 0x50; i += 3) {
            uint8_t op = memory_->read_common(0xFA00 + i);
            uint16_t addr = memory_->read_common(0xFA00 + i + 1) |
                           (memory_->read_common(0xFA00 + i + 2) << 8);
            if (op == 0xC3) {
                std::cerr << "  FA" << std::hex << std::setw(2) << std::setfill('0') << i
                          << ": JP " << std::setw(4) << addr << "H\n";
            }
        }

        // Dump low memory (0x00-0x40) in bank 0
        std::cerr << "Low memory (0x0000-0x0040) in bank 0:\n";
        for (int i = 0; i < 0x40; i += 16) {
            std::cerr << "  " << std::hex << std::setw(4) << std::setfill('0') << i << ": ";
            for (int j = 0; j < 16; j++) {
                std::cerr << std::setw(2) << (int)memory_->read_bank(0, i + j) << " ";
            }
            std::cerr << "\n";
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

            // Auto-start clock after boot completes (5M instructions)
            // MP/M II XDOS should call STARTCLOCK but doesn't seem to
            static bool auto_started = false;
            if (!auto_started && instruction_count_.load() > 5000000) {
                if (g_debug_enabled) std::cerr << "[CLOCK] Auto-starting clock after boot\n";
                xios_->start_clock();
                auto_started = true;
            }

            // Request tick interrupt if clock is enabled
            if (xios_->clock_enabled()) {
                static int tick_count_local = 0;
                tick_count_local++;
                // Skip first few ticks to let boot complete initial setup
                if (tick_count_local < 10) continue;
                // Log ticks only with g_debug_enabled
                if (g_debug_enabled && (tick_count_local <= 10 || (tick_count_local % 120) == 0)) {
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
                static int tick_delivered = 0;
                if (cpu_->regs.IFF1) {
                    iff_zero_count = 0;  // Reset counter
                    cpu_->request_rst(7);  // RST 38H
                    tick_delivered++;
                    if (g_debug_enabled && tick_delivered <= 5) {
                        std::cerr << "[TICK] Delivered RST38 #" << tick_delivered << "\n";
                    }
                } else {
                    iff_zero_count++;
                    // If IFF has been 0 for too many ticks, force-enable
                    // This works around XDOS not EI'ing when returning to process
                    if (iff_zero_count >= 5 && tick_count_local > 15) {
                        if (g_debug_enabled) std::cerr << "[TICK] Force-enabling interrupts\n";
                        cpu_->regs.IFF1 = 1;
                        cpu_->regs.IFF2 = 1;
                        cpu_->request_rst(7);
                        tick_delivered++;
                        iff_zero_count = 0;  // Reset after force-enable
                    }
                }
                xios_->tick();
            }

            // Check for one-second tick
            if (++tick_count_ >= 60) {
                tick_count_ = 0;
                xios_->one_second_tick();

                // Create missing TMPs and dump TMPD area after boot
                static int sec_count = 0;
                sec_count++;

                // NOTE: TMP creation fix is DISABLED because it breaks the ready list.
                // The system works for console 7 as-is (XDOS creates TMP7 at slot 0).
                // TODO: Find a better approach that properly links TMPs into the scheduler.
                // if (sec_count == 1) {
                //     create_missing_tmps();
                // }

                if (g_debug_enabled && sec_count == 2) {
                    // TMPD is at FD00H per GENSYS memory layout
                    uint16_t tmpd_addr = 0xFD00;
                    std::cerr << "[TICK] TMPD at 0x" << std::hex << tmpd_addr
                              << " dump after 2 seconds:\n";
                    for (int tmp = 0; tmp < num_consoles_; tmp++) {
                        int base = tmpd_addr + (tmp * 64);
                        std::cerr << "  TMP" << tmp << " at 0x" << std::hex << base << ": ";
                        for (int i = 0; i < 32; i++) {
                            std::cerr << std::setw(2) << std::setfill('0')
                                      << (int)memory_->read_common(base + i) << " ";
                        }
                        // Show name at offset 6
                        std::cerr << " name='";
                        for (int i = 6; i < 14; i++) {
                            uint8_t ch = memory_->read_common(base + i);
                            std::cerr << (ch >= 0x20 && ch < 0x7F ? (char)ch : '.');
                        }
                        std::cerr << "' con=" << (int)memory_->read_common(base + 0x0E);
                        std::cerr << "\n" << std::dec;
                    }
                    // Search for "Tmp" process names in common memory (C000-FFFF)
                    std::cerr << "[TICK] Searching for 'Tmp' process descriptors in common memory:\n";
                    for (uint16_t addr = 0xC000; addr < 0xFF00; addr++) {
                        // Look for "Tmp" at offset 6 (name field in process descriptor)
                        if (memory_->read_common(addr + 6) == 'T' &&
                            memory_->read_common(addr + 7) == 'm' &&
                            memory_->read_common(addr + 8) == 'p') {
                            std::cerr << "  PD at 0x" << std::hex << addr << ": ";
                            for (int i = 0; i < 24; i++) {
                                std::cerr << std::setw(2) << std::setfill('0')
                                          << (int)memory_->read_common(addr + i) << " ";
                            }
                            std::cerr << " name='";
                            for (int i = 6; i < 14; i++) {
                                uint8_t ch = memory_->read_common(addr + i);
                                std::cerr << (ch >= 0x20 && ch < 0x7F ? (char)ch : '.');
                            }
                            std::cerr << "' con=" << (int)memory_->read_common(addr + 0x0E);
                            std::cerr << "\n" << std::dec;
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
        // IMPORTANT: Switch to bank 0 before interrupt handling
        // because the interrupt vector at 0x0038 is in bank 0's memory
        // (banked area 0x0000-0xBFFF is per-bank, only common area is shared)
        uint8_t saved_bank = memory_->current_bank();
        if (cpu_->int_pending) {
            memory_->select_bank(0);  // Switch to system bank for interrupt vector
        }

        uint16_t pc_before = cpu_->regs.PC.get_pair16();

        // Debug: log state before check_interrupts (only with g_debug_enabled)
        static int chk_count = 0;
        if (g_debug_enabled && cpu_->int_pending) {
            chk_count++;
            if (chk_count <= 10) {
                std::cerr << "[CHK] #" << chk_count
                          << " int_pending=" << cpu_->int_pending
                          << " IFF1=" << (int)cpu_->regs.IFF1
                          << " PC=0x" << std::hex << pc_before << std::dec << "\n";
            }
        }

        bool delivered = cpu_->check_interrupts();
        uint16_t pc_after = cpu_->regs.PC.get_pair16();

        // Log when interrupt is actually delivered (only with g_debug_enabled)
        if (g_debug_enabled && delivered) {
            static int int_count = 0;
            int_count++;
            if (int_count <= 10) {
                std::cerr << "[INT] #" << int_count
                          << " PC: 0x" << std::hex << pc_before
                          << " -> 0x" << pc_after << std::dec << "\n";
            }
        }
        if (!delivered && saved_bank != 0 && cpu_->int_pending) {
            // Interrupt pending but not delivered (IFF1=0), restore bank
            memory_->select_bank(saved_bank);
        }

        // Execute one instruction
        g_debug_last_pc = cpu_->regs.PC.get_pair16();
        cpu_->execute();
        instruction_count_++;

        // Trace XDOS function calls - POLL (131), FLAGWAIT (132), FLAGSET (133)
        if (g_debug_enabled) {
            uint16_t pc = cpu_->regs.PC.get_pair16();
            uint8_t c = cpu_->regs.BC.get_low();
            uint8_t e = cpu_->regs.DE.get_low();
            // Trace POLL calls (C=131)
            if (c == 131) {
                static int poll_xdos_count = 0;
                poll_xdos_count++;
                uint16_t de = cpu_->regs.DE.get_pair16();
                // Always trace poll table calls (DE >= 256), limit simple polls
                if (de >= 256 || poll_xdos_count <= 30) {
                    std::cerr << "[POLL XDOS] PC=0x" << std::hex << pc
                              << " DE=0x" << de;
                    if (de < 256) {
                        std::cerr << " (simple: console " << std::dec << (e/2)
                                  << (e & 1 ? " input" : " output") << ")";
                    } else {
                        std::cerr << " (POLL TABLE at " << std::hex << de << ")";
                    }
                    std::cerr << std::dec << " #" << poll_xdos_count << "\n";
                }
            }
            // When we're at a CALL instruction with C=133, trace it
            if (c == 133 && e >= 3 && e <= 10) {  // Console input flags
                static int flagset_call_count = 0;
                flagset_call_count++;
                if (flagset_call_count <= 30) {
                    std::cerr << "[FLAGSET CALL] PC=0x" << std::hex << pc
                              << " C=" << std::dec << (int)c
                              << " E=" << (int)e
                              << " (console " << (e-3) << " flag)"
                              << " #" << flagset_call_count << "\n";
                }
            }
        }

        // Periodic Z80 status log (only in debug mode)
        if (g_debug_enabled) {
            static uint64_t last_log_count = 0;
            uint64_t count = instruction_count_.load();
            if (count >= last_log_count + 1000000) {
                std::cerr << "[Z80] " << (count / 1000000) << "M instructions executed"
                          << ", PC=0x" << std::hex << cpu_->regs.PC.get_pair16()
                          << ", bank=" << std::dec << (int)memory_->current_bank() << "\n";
                last_log_count = count;
            }
        }

        // Trace XDOS calls (only in debug mode)
        if (g_debug_enabled) {
            uint16_t pc = cpu_->regs.PC.get_pair16();

            // Detect XDOS entry by looking for calls in common memory with C=133 (FLAGSET)
            // and IFF1=0 (interrupts disabled = we're in interrupt handler)
            uint8_t func = cpu_->regs.BC.get_low();
            if (func == 133 && pc >= 0xC000 && pc < 0xFF00) {  // Potential XDOS call with FLAGSET
                static int flagset_trace_count = 0;
                flagset_trace_count++;
                if (flagset_trace_count <= 50) {
                    uint8_t flag = cpu_->regs.DE.get_low();
                    std::cerr << "[FLAGSET DETECT] PC=0x" << std::hex << pc
                              << " flag=" << std::dec << (int)flag
                              << " IFF1=" << (int)cpu_->regs.IFF1
                              << " #" << flagset_trace_count << "\n";
                }
            }

            // Dump SYSDAT console count on first XDOS call
            static bool dumped_sysdat = false;
            if (!dumped_sysdat && pc >= 0xC000 && pc < 0xFF00) {
                dumped_sysdat = true;
                uint8_t nmb_cns = memory_->read_common(0xFF01);
                std::cerr << "[SYSDAT] Console count at 0xFF01 = " << (int)nmb_cns << "\n";
            }

            // Check if we're at an XDOS entry point
            // XDOS entry is in the range E000-EF00, check every 3 bytes for JP pattern
            // Simplify: just check for XDOS function codes in common memory range
            // Check for XDOS calls - include all common memory ranges
            bool at_xdos = (pc >= 0xE000 && pc <= 0xEF00) ||
                           (pc >= 0xCC00 && pc <= 0xD000) ||
                           (pc >= 0xFC00 && pc <= 0xFF00) ||  // XIOSJMP area
                           (pc >= 0xC100 && pc <= 0xCB00);    // BNKXIOS area
            if (at_xdos) {
                // Debug: track FLAGSET calls (these wake up waiting processes)
                if (func == 133) {  // FLAGSET
                    uint8_t flag = cpu_->regs.DE.get_low();
                    static int flagset_count = 0;
                    flagset_count++;
                    // Log ALL flags
                    if (flagset_count <= 30) {
                        std::cerr << "[FLAGSET] flag=" << (int)flag
                                  << " PC=0x" << std::hex << pc << std::dec
                                  << " #" << flagset_count << "\n";
                    }
                }

                // XDOS function 144 (0x90) = CREATE PROCESS
                // Just trace CREATE calls - the post-boot workaround handles TMP fixes
                if (func == 0x90) {
                    static int create_count = 0;
                    create_count++;
                    uint16_t pd_addr = cpu_->regs.DE.get_pair16();

                    std::string name;
                    for (int i = 6; i < 14; i++) {
                        uint8_t ch = memory_->read_common(pd_addr + i) & 0x7F;
                        if (ch >= 0x20 && ch < 0x7F) {
                            name += (char)ch;
                        }
                    }
                    if (create_count <= 30 || name.substr(0, 2) == "Tm") {
                        std::cerr << "[XDOS CREATE] #" << create_count
                                  << " PD=0x" << std::hex << pd_addr
                                  << " name='" << name << "'"
                                  << " console=" << std::dec << (int)memory_->read_common(pd_addr + 0x0E)
                                  << "\n";
                    }
                }
                // XDOS function 143 (0x8F) = TERMINATE - trace first few
                static int terminate_count = 0;
                if (func == 0x8F) {
                    terminate_count++;
                    if (terminate_count <= 5) {
                        std::cerr << "[XDOS TERMINATE] #" << terminate_count
                                  << " E=" << (int)cpu_->regs.DE.get_low() << "\n";
                    }
                }
            }
        }
    }
}

void Z80Thread::create_missing_tmps() {
    // Workaround for XDOS bug: it only creates one TMP (the highest numbered console).
    // This function creates the missing TMP process descriptors after boot.
    //
    // XDOS bug: It creates TMP7 (console 7) but puts it in slot 0 (FD00) instead of slot 7 (FEC0).
    // We need to:
    // 1. Find the existing TMP and copy it as a template
    // 2. Move it to its correct slot if necessary
    // 3. Create missing TMPs for other consoles
    //
    // TMPD area layout (at 0xFD00, 64 bytes per TMP):
    //   Slot 0: 0xFD00, Slot 1: 0xFD40, Slot 2: 0xFD80, Slot 3: 0xFDC0
    //   Slot 4: 0xFE00, Slot 5: 0xFE40, Slot 6: 0xFE80, Slot 7: 0xFEC0
    //
    // Process Descriptor structure (relevant fields):
    //   Offset 0-1:  Link to next PD
    //   Offset 2:    Status
    //   Offset 3:    Priority (0xC6 = 198 for TMP)
    //   Offset 4-5:  Stack pointer
    //   Offset 6-13: Process name (8 bytes, last byte often has bit 7 set)
    //   Offset 14:   Console number

    const uint16_t TMPD_BASE = 0xFD00;
    const int TMP_SIZE = 64;

    // Find the existing TMP (XDOS puts it at slot 0 regardless of console number)
    uint16_t existing_tmp_addr = TMPD_BASE;
    int existing_console = memory_->read_common(existing_tmp_addr + 0x0E);

    // Check if TMP exists (name should contain 'mp' at offset 7-8)
    uint8_t name_byte1 = memory_->read_common(existing_tmp_addr + 7);  // Should be 'm'
    uint8_t name_byte2 = memory_->read_common(existing_tmp_addr + 8);  // Should be 'p'
    std::cerr << "[TMP FIX] Checking FD00: byte7=0x" << std::hex << (int)name_byte1
              << " byte8=0x" << (int)name_byte2 << std::dec
              << " (expecting 'm'=0x6d, 'p'=0x70)\n";
    if (name_byte1 != 'm' || name_byte2 != 'p') {
        std::cerr << "[TMP FIX] No valid TMP found at " << std::hex << existing_tmp_addr
                  << ", skipping TMP creation\n" << std::dec;
        return;
    }

    std::cerr << "[TMP FIX] Found existing TMP" << existing_console
              << " in slot 0 (0x" << std::hex << existing_tmp_addr << ")" << std::dec << "\n";

    // Copy the existing TMP structure as a template
    uint8_t tmp_template[TMP_SIZE];
    for (int i = 0; i < TMP_SIZE; i++) {
        tmp_template[i] = memory_->read_common(existing_tmp_addr + i);
    }

    // Calculate correct slot for the existing TMP
    uint16_t correct_slot_addr = TMPD_BASE + (existing_console * TMP_SIZE);

    // If the existing TMP is in the wrong slot, copy it to the correct slot
    if (existing_tmp_addr != correct_slot_addr) {
        std::cerr << "[TMP FIX] Moving TMP" << existing_console
                  << " from slot 0 to slot " << existing_console
                  << " (0x" << std::hex << correct_slot_addr << ")" << std::dec << "\n";
        for (int i = 0; i < TMP_SIZE; i++) {
            memory_->write_common(correct_slot_addr + i, tmp_template[i]);
        }
    }

    // CONSOLE.DAT area: each console has stack/buffer space
    // The existing TMP has a valid stack pointer, use it to calculate base
    uint16_t existing_stack = memory_->read_common(existing_tmp_addr + 4) |
                              (memory_->read_common(existing_tmp_addr + 5) << 8);
    // Stack is typically in USERSYS.STK area, allocated per console

    // Create TMPs for all consoles
    for (int con = 0; con < num_consoles_; con++) {
        uint16_t tmp_addr = TMPD_BASE + (con * TMP_SIZE);

        // Skip the console that already has a valid TMP
        if (con == existing_console) {
            // Already exists and was moved to correct slot
            continue;
        }

        // Check if this slot already has a valid TMP for THIS console
        uint8_t check_m = memory_->read_common(tmp_addr + 7);
        uint8_t check_p = memory_->read_common(tmp_addr + 8);
        int check_con = memory_->read_common(tmp_addr + 0x0E);
        if (check_m == 'm' && check_p == 'p' && check_con == con) {
            std::cerr << "[TMP FIX] TMP" << con << " already exists at 0x"
                      << std::hex << tmp_addr << std::dec << "\n";
            continue;
        }

        std::cerr << "[TMP FIX] Creating TMP" << con
                  << " at 0x" << std::hex << tmp_addr << std::dec << "\n";

        // Copy template
        for (int i = 0; i < TMP_SIZE; i++) {
            memory_->write_common(tmp_addr + i, tmp_template[i]);
        }

        // Set console number
        memory_->write_common(tmp_addr + 0x0E, con);

        // Set process name: "Tmpn    " where n is console number
        memory_->write_common(tmp_addr + 6, 'T');
        memory_->write_common(tmp_addr + 7, 'm');
        memory_->write_common(tmp_addr + 8, 'p');
        memory_->write_common(tmp_addr + 9, '0' + con);  // Console number as digit
        memory_->write_common(tmp_addr + 10, ' ');
        memory_->write_common(tmp_addr + 11, ' ');
        memory_->write_common(tmp_addr + 12, ' ');
        memory_->write_common(tmp_addr + 13, 0xA0);  // Space with bit 7 set (MP/M convention)

        // Set up stack pointer: use same offset from existing TMP's stack
        // This is a simplification - ideally each TMP has unique stack space
        // For now, offset by console number to avoid conflicts
        uint16_t stack_offset = (existing_console - con) * 256;
        uint16_t new_stack = existing_stack + stack_offset;
        memory_->write_common(tmp_addr + 4, new_stack & 0xFF);
        memory_->write_common(tmp_addr + 5, (new_stack >> 8) & 0xFF);

        // Clear link pointer (will be set when added to ready list)
        memory_->write_common(tmp_addr + 0, 0);
        memory_->write_common(tmp_addr + 1, 0);
    }

    // Clear slot 0 if it was the original location of a misplaced TMP
    if (existing_tmp_addr != correct_slot_addr && existing_console != 0) {
        // Slot 0 should now contain TMP0, not the old TMP7
        // The loop above already created TMP0 in slot 0
    }

    std::cerr << "[TMP FIX] Created TMPs for " << num_consoles_ << " consoles\n";

    // Dump the TMPD area to verify
    std::cerr << "[TMP FIX] TMPD dump after creation:\n";
    for (int tmp = 0; tmp < num_consoles_; tmp++) {
        uint16_t base = TMPD_BASE + (tmp * TMP_SIZE);
        std::cerr << "  TMP" << tmp << " at 0x" << std::hex << base << ": ";
        // Show first 16 bytes
        for (int i = 0; i < 16; i++) {
            std::cerr << std::setw(2) << std::setfill('0')
                      << (int)memory_->read_common(base + i) << " ";
        }
        std::cerr << " name='";
        for (int i = 6; i < 14; i++) {
            uint8_t ch = memory_->read_common(base + i);
            std::cerr << (ch >= 0x20 && ch < 0x7F ? (char)ch : '.');
        }
        std::cerr << "' con=" << (int)memory_->read_common(base + 0x0E);
        std::cerr << "\n" << std::dec;
    }

    // TODO: The created TMPs are not yet on the ready list.
    // They need to be registered with XDOS. For now, we've populated
    // the TMPD area so the structures exist. The TMPs still need to
    // be started via XDOS CREATE or direct ready list manipulation.
}
