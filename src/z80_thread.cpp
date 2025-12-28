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
    // Create memory (4 banks = 128KB + 32KB common)
    memory_ = std::make_unique<BankedMemory>(4);

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
    uint8_t nmb_rsps = sysdat[125];        // Number of RSPs
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
        uint8_t seg_attr = sysdat[16 + i * 4 + 2];
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
    std::cerr << "[Z80] Starting at PC=0x" << std::hex
              << cpu_->regs.PC.get_pair16() << std::dec << std::endl;

    // Trace disabled by default - set to non-zero to enable periodic traces
    uint64_t trace_interval = 0;  // Was 100000
    uint64_t next_trace = trace_interval;

    while (!stop_requested_.load()) {
        // Periodic instruction trace (disabled when trace_interval == 0)
        if (trace_interval > 0 && instruction_count_.load() >= next_trace) {
            std::cerr << "[Z80 TRACE] " << instruction_count_.load() << " instructions, PC=0x"
                      << std::hex << cpu_->regs.PC.get_pair16() << std::dec << std::endl;
            next_trace += trace_interval;
        }
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

        // Check for timer interrupt
        if (now >= next_tick_) {
            next_tick_ += TICK_INTERVAL;

            // Deliver tick interrupt if clock is enabled
            if (xios_->clock_enabled() && cpu_->regs.IFF1) {
                deliver_tick_interrupt();
            }

            // Check for one-second tick
            if (++tick_count_ >= 60) {
                tick_count_ = 0;
                xios_->one_second_tick();
            }
        }

        // Check for XIOS trap before executing
        uint16_t pc = cpu_->regs.PC.get_pair16();

        // XIOSJMP interception: redirect FC00-FC5A to emulator's XIOS handler
        if (pc >= 0xFC00 && pc < 0xFC5D) {
            uint16_t offset = pc - 0xFC00;
            // Valid XIOS entry points are at 3-byte intervals
            if (offset % 3 == 0) {
                // Call emulator's XIOS handler via port dispatch mechanism
                xios_->handle_port_dispatch(offset);
                // Pop return address from stack and return
                uint16_t sp = cpu_->regs.SP.get_pair16();
                uint8_t lo = memory_->fetch_mem(sp);
                uint8_t hi = memory_->fetch_mem(sp + 1);
                uint16_t ret_addr = (hi << 8) | lo;
                cpu_->regs.SP.set_pair16(sp + 2);
                cpu_->regs.PC.set_pair16(ret_addr);

                instruction_count_++;
                continue;  // Skip normal instruction execution
            }
        }

        // Check for HALT instruction (0x76) - handle specially for MP/M
        uint8_t opcode = memory_->fetch_mem(pc);
        // qkz80 library calls exit() on HALT, but MP/M uses HALT in idle loop
        if (opcode == 0x76) {
            // Advance PC past HALT
            cpu_->regs.PC.set_pair16(pc + 1);
            instruction_count_++;

            // Wait for timer interrupt or stop request or timeout
            while (!stop_requested_.load() && !xios_->clock_enabled()) {
                // Check timeout while waiting
                if (timeout_seconds_ > 0) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::steady_clock::now() - start_time_).count();
                    if (elapsed >= timeout_seconds_) {
                        std::cerr << "\n[TIMEOUT] Timed out while HALTed after " << elapsed << " seconds\n";
                        std::cerr << "[TIMEOUT] PC=0x" << std::hex << pc
                                  << " SP=0x" << cpu_->regs.SP.get_pair16()
                                  << " bank=" << std::dec << (int)memory_->current_bank()
                                  << " instructions=" << instruction_count_.load() << "\n";
                        timed_out_.store(true);
                        stop_requested_.store(true);
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            // If interrupts are enabled and clock is running, wait for tick
            if (cpu_->regs.IFF1 && xios_->clock_enabled()) {
                // Sleep until next tick
                auto now = std::chrono::steady_clock::now();
                if (now < next_tick_) {
                    std::this_thread::sleep_until(next_tick_);
                }
            }
            continue;
        }

        // Execute one instruction
        cpu_->execute();
        instruction_count_++;

        // TODO: Check for I/O instructions (IN/OUT) and handle them
    }
}

void Z80Thread::deliver_tick_interrupt() {
    // MP/M uses RST 7 (or configurable) for timer interrupt
    // Push PC, jump to interrupt vector

    // Save current PC on stack
    uint16_t sp = cpu_->regs.SP.get_pair16();
    uint16_t pc = cpu_->regs.PC.get_pair16();
    sp -= 2;
    cpu_->regs.SP.set_pair16(sp);
    memory_->store_mem(sp, pc & 0xFF);
    memory_->store_mem(sp + 1, (pc >> 8) & 0xFF);

    // Set preempted flag
    xios_->set_preempted(true);

    // Disable interrupts
    cpu_->regs.IFF1 = 0;
    cpu_->regs.IFF2 = 0;

    // Jump to interrupt handler (RST 38H = address 0x0038)
    cpu_->regs.PC.set_pair16(0x0038);

    // Signal tick to XIOS
    xios_->tick();
}
