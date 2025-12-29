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

    // CRITICAL: Patch commonbase PDISP and XDOS entries to point to real XDOS
    uint16_t xios_base = bnkxios_base * 256;
    uint16_t xiosjmp_base = xios_jmp_tbl_base * 256;
    // Layout:
    //   +0: COLDBOOT (JP DO_BOOT) - returns HL=commonbase, OK as is
    //   +3: SWTUSER (JP DO_SWTUSER) - bank switch, OK as is
    //   +6: SWTSYS (JP DO_SWTSYS) - bank switch, OK as is
    //   +9: PDISP (JP) - MUST point to real XDOS process dispatcher
    //   +12: XDOS (JP) - MUST point to real XDOS entry at xdos_base
    //   +15: SYSDAT (DW) - address of system data
    uint16_t commonbase_addr = xios_base + 0x4B;  // COMMONBASE is at offset 0x4B after 25 JP entries
    uint16_t real_xdos = xdos_base * 256;

    // XDOS entry at commonbase+12 -> should JP to real XDOS (xdos_base*256)
    // The JP instruction is: C3 lo hi
    memory_->write_common(commonbase_addr + 12, 0xC3);  // JP opcode
    memory_->write_common(commonbase_addr + 13, real_xdos & 0xFF);
    memory_->write_common(commonbase_addr + 14, (real_xdos >> 8) & 0xFF);

    // PDISP entry at commonbase+9 -> should JP to XDOS+3 (dispatcher entry)
    uint16_t pdisp_addr = real_xdos + 3;
    memory_->write_common(commonbase_addr + 9, 0xC3);   // JP opcode
    memory_->write_common(commonbase_addr + 10, pdisp_addr & 0xFF);
    memory_->write_common(commonbase_addr + 11, (pdisp_addr >> 8) & 0xFF);

    // Debug: show the patched commonbase structure
    std::cerr << "\n[PATCH] Commonbase at 0x" << std::hex << commonbase_addr << ":\n";
    std::cerr << "  PDISP (+9) -> JP 0x" << std::setw(4) << std::setfill('0') << pdisp_addr << "\n";
    std::cerr << "  XDOS  (+c) -> JP 0x" << std::setw(4) << std::setfill('0') << real_xdos << "\n";

    // Show interrupt handler code from DO_SYSINIT area
    std::cerr << "[DEBUG] Code at C800 (DO_SYSINIT area):\n  ";
    for (int i = 0; i < 64; i++) {
        if (i > 0 && i % 16 == 0) std::cerr << "\n  ";
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_common(0xC800 + i) << " ";
    }
    std::cerr << std::dec << std::setfill(' ') << "\n";

    // Show INTHND at 0xC831 (interrupt handler proper)
    std::cerr << "[DEBUG] INTHND at C831 (interrupt handler):\n  ";
    for (int i = 0; i < 64; i++) {
        if (i > 0 && i % 16 == 0) std::cerr << "\n  ";
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_common(0xC831 + i) << " ";
    }
    std::cerr << std::dec << std::setfill(' ') << "\n";

    // Show commonbase area
    std::cerr << "[DEBUG] Commonbase at " << std::hex << commonbase_addr << ":\n  ";
    for (int i = 0; i < 24; i++) {
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_common(commonbase_addr + i) << " ";
    }
    std::cerr << std::dec << std::setfill(' ') << "\n";

    // Show memory at 0xe9d1 (the descriptor address we keep seeing)
    std::cerr << "[DEBUG] Memory at E9D0:\n  ";
    for (int i = 0; i < 16; i++) {
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_common(0xE9D0 + i) << " ";
    }
    std::cerr << std::dec << std::setfill(' ') << "\n";

    // Also show memory segments in SYSTEM.DAT at FF10
    std::cerr << "[DEBUG] Memory segments at FF10 (SYSDAT+0x10):\n";
    for (int seg = 0; seg < 6; seg++) {
        uint16_t addr = 0xFF10 + seg * 4;
        uint8_t base = memory_->read_common(addr);
        uint8_t size = memory_->read_common(addr + 1);
        uint8_t attr = memory_->read_common(addr + 2);
        uint8_t bank = memory_->read_common(addr + 3);
        std::cerr << "  Seg " << seg << " @ " << std::hex << addr << ": base="
                  << std::setw(2) << (int)base << " size=" << std::setw(2) << (int)size
                  << " attr=" << std::setw(2) << (int)attr << " bank=" << (int)bank
                  << std::dec << "\n";
    }

    // Show XDOS entry at CE00
    std::cerr << "[DEBUG] XDOS at CE00:\n  ";
    for (int i = 0; i < 32; i++) {
        if (i > 0 && i % 16 == 0) std::cerr << "\n  ";
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_common(0xCE00 + i) << " ";
    }
    std::cerr << std::dec << std::setfill(' ') << "\n";

    // Show sysdat variable at CE0C (should be 0xFF00)
    uint16_t sysdat_var = memory_->read_common(0xCE0C) | (memory_->read_common(0xCE0D) << 8);
    std::cerr << "[DEBUG] sysdat variable at CE0C = 0x" << std::hex << sysdat_var << std::dec << "\n";

    // Show key SYSTEM.DAT offsets for TMP initialization
    if (sysdat_var >= 0xFF00) {
        uint8_t tmpd_base = memory_->read_common(sysdat_var + 243);  // offset 243 = tmpd$base
        uint8_t console_dat = memory_->read_common(sysdat_var + 244); // offset 244 = console$dat$base
        uint8_t tmp_base = memory_->read_common(sysdat_var + 247);   // offset 247 = tmp$base
        std::cerr << "[DEBUG] SYSTEM.DAT TMP fields:\n";
        std::cerr << "  tmpd$base (243) = 0x" << std::hex << (int)tmpd_base
                  << " -> TMPD at 0x" << (tmpd_base * 256) << "\n";
        std::cerr << "  console$dat (244) = 0x" << (int)console_dat
                  << " -> console data at 0x" << (console_dat * 256) << "\n";
        std::cerr << "  tmp$base (247) = 0x" << (int)tmp_base
                  << " -> TMP code at 0x" << (tmp_base * 256) << std::dec << "\n";
    }

    // Show TMPD.DAT at FE00 (TMP process descriptor data)
    std::cerr << "[DEBUG] TMPD at FE00:\n  ";
    for (int i = 0; i < 64; i++) {
        if (i > 0 && i % 16 == 0) std::cerr << "\n  ";
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_common(0xFE00 + i) << " ";
    }
    std::cerr << std::dec << std::setfill(' ') << "\n";

    // Show dispatcher area at DD38
    std::cerr << "[DEBUG] Dispatcher at DD38:\n  ";
    for (int i = 0; i < 32; i++) {
        if (i > 0 && i % 16 == 0) std::cerr << "\n  ";
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_common(0xDD38 + i) << " ";
    }
    std::cerr << std::dec << std::setfill(' ') << "\n";

    // Show SWTUSER at C7EC and SWTSYS at C7F1
    std::cerr << "[DEBUG] SWTUSER at C7EC:\n  ";
    for (int i = 0; i < 16; i++) {
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_common(0xC7EC + i) << " ";
    }
    std::cerr << "\n[DEBUG] SWTSYS at C7F1:\n  ";
    for (int i = 0; i < 16; i++) {
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_common(0xC7F1 + i) << " ";
    }
    std::cerr << std::dec << std::setfill(' ') << "\n";

    // Check user banks for TMP code
    std::cerr << "[DEBUG] Bank 1 at 0x0000 (first 16 bytes):\n  ";
    for (int i = 0; i < 16; i++) {
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_bank(1, i) << " ";
    }
    std::cerr << "\n[DEBUG] Bank 1 at 0x0100 (TPA start):\n  ";
    for (int i = 0; i < 16; i++) {
        std::cerr << std::hex << std::setw(2) << std::setfill('0')
                  << (int)memory_->read_bank(1, 0x0100 + i) << " ";
    }
    std::cerr << std::dec << std::setfill(' ') << "\n";

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

    // Trace first N instructions to see initialization
    int init_trace = 0;
    const int INIT_TRACE_LIMIT = 0;  // Set to e.g. 200 to trace startup

    // Trace disabled by default - set to non-zero to enable periodic traces
    uint64_t trace_interval = 0;  // Was 100000
    uint64_t next_trace = trace_interval;

    // Debug output disabled for cleaner console output
    // std::cerr << "[Z80] Entering main loop, stop_requested=" << stop_requested_.load() << std::endl;
    while (!stop_requested_.load()) {
        // static bool first_loop = true;
        // if (first_loop) {
        //     std::cerr << "[Z80] First loop iteration" << std::endl;
        //     first_loop = false;
        // }
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

            // Deliver tick interrupt if clock is enabled and interrupts are enabled
            static int tick_count_trace = 0;
            tick_count_trace++;
            if (tick_count_trace <= 20) {
                std::cerr << "[TICK #" << tick_count_trace << "] clock="
                          << xios_->clock_enabled() << " IFF=" << (int)cpu_->regs.IFF1
                          << " PC=0x" << std::hex << cpu_->regs.PC.get_pair16() << std::dec << "\n";
            }
            if (xios_->clock_enabled() && cpu_->regs.IFF1) {
                deliver_tick_interrupt();
            }

            // Check for one-second tick
            if (++tick_count_ >= 60) {
                tick_count_ = 0;
                xios_->one_second_tick();
            }
        }

        // Check for HALT instruction (0x76) - handle specially for MP/M
        uint16_t pc = cpu_->regs.PC.get_pair16();
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
    }
}

void Z80Thread::deliver_tick_interrupt() {
    // MP/M uses RST 38H for timer interrupt
    // Push PC on stack and jump to interrupt vector

    // Save current PC on stack
    uint16_t sp = cpu_->regs.SP.get_pair16();
    uint16_t pc = cpu_->regs.PC.get_pair16();

    static int int_count = 0;
    int_count++;
    if (int_count <= 10) {
        // Show what the interrupt vector contains
        uint8_t v0 = memory_->fetch_mem(0x0038);
        uint8_t v1 = memory_->fetch_mem(0x0039);
        uint8_t v2 = memory_->fetch_mem(0x003A);
        std::cerr << "[INT #" << int_count << "] from PC=0x" << std::hex << pc
                  << " SP=0x" << sp << " vec@38=" << std::setfill('0') << std::setw(2)
                  << (int)v0 << " " << std::setw(2) << (int)v1 << " " << std::setw(2) << (int)v2
                  << " bank=" << std::dec << (int)memory_->current_bank() << "\n";
    }

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
