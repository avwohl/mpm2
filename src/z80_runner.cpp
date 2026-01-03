// z80_runner.cpp - Z80 CPU emulation runner implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "z80_runner.h"
#include "mpm_cpu.h"
#include "banked_mem.h"
#include "xios.h"
#include "console.h"
#include "disk.h"
#include <fstream>
#include <cstring>
#include <iostream>
#include <iomanip>

Z80Runner::Z80Runner()
    : running_(false)
    , stop_requested_(false)
    , instruction_count_(0)
    , tick_count_(0)
{
}

Z80Runner::~Z80Runner() {
    stop();
}

bool Z80Runner::init(const std::string& boot_image) {
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

bool Z80Runner::boot_from_disk() {
    // Boot from disk sector 0 - the cold start loader
    // This reads physical sector 0 from drive A into memory at 0x0000
    // and starts execution there.

    // Create memory (8 banks for MP/M II)
    memory_ = std::make_unique<BankedMemory>(8);

    // Create CPU with memory
    cpu_ = std::make_unique<MpmCpu>(memory_.get());
    cpu_->set_cpu_mode(qkz80::MODE_Z80);

    // Create XIOS
    xios_ = std::make_unique<XIOS>(cpu_.get(), memory_.get());

    // Connect CPU to XIOS and banked memory for port dispatch
    cpu_->set_xios(xios_.get());
    cpu_->set_banked_mem(memory_.get());

    // Get disk A
    Disk* disk = DiskSystem::instance().get(0);
    if (!disk || !disk->is_open()) {
        std::cerr << "Cannot boot: no disk mounted on drive A:\n";
        return false;
    }

    std::cout << "Booting from disk A: sector 0...\n";

    // Read sector 0 into memory at 0x0000
    // For hd1k format, sector 0 is 512 bytes
    disk->set_track(0);
    disk->set_sector(0);

    uint8_t boot_sector[512];
    if (disk->read_sector(boot_sector) != 0) {
        std::cerr << "Failed to read boot sector\n";
        return false;
    }

    // Load boot sector into bank 0 at address 0x0000
    memory_->load(0, 0x0000, boot_sector, disk->sector_size());

    std::cout << "Loaded " << disk->sector_size() << " bytes from sector 0\n";

    // Verify it looks like valid code (starts with DI or JP)
    uint8_t first_byte = boot_sector[0];
    if (first_byte != 0xF3 && first_byte != 0xC3) {
        std::cerr << "Warning: boot sector doesn't start with DI (0xF3) or JP (0xC3)\n";
        std::cerr << "First byte: 0x" << std::hex << (int)first_byte << std::dec << "\n";
    }

    // Set PC to 0x0000 to start execution at the cold boot loader
    cpu_->regs.PC.set_pair16(0x0000);

    // Set up initial stack (will be reset by the boot loader)
    cpu_->regs.SP.set_pair16(0x0100);

    std::cout << "Starting execution at 0x0000\n\n";

    return true;
}

bool Z80Runner::load_mpm_sys(const std::string& mpm_sys_path) {
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

    // Tell ConsoleManager how many consoles MP/M is configured for
    ConsoleManager::instance().set_active_consoles(nmb_cns);

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
    for (int i = 0; i < 100; i++) {  // Run up to 100 instructions
        cpu_->execute();
        if (cpu_->regs.PC.get_pair16() == 0x0000) {
            break;
        }
    }

    // Set PC to entry point for main execution
    // (SYSINIT returned to 0x0000, we need to start at the real entry point)
    cpu_->regs.PC.set_pair16(entry_point);
    cpu_->regs.SP.set_pair16(sysdat_addr);  // Reset SP to a sensible value

    // Find TICKN address for later checks
    for (uint16_t addr = bnkxios_base * 256; addr < bnkxios_base * 256 + 0x200; addr++) {
        uint8_t b1 = memory_->read_common(addr + 1);  // CNT60
        uint8_t b2 = memory_->read_common(addr + 2);  // PREEMP
        uint8_t b3 = memory_->read_common(addr + 3);  // CONST60
        if (b1 == 60 && b2 == 0 && b3 == 60) {
            tickn_addr_ = addr;
            break;
        }
    }

    // Copy interrupt vector to ALL banks
    uint8_t rst38_op = memory_->read_bank(0, 0x0038);
    uint16_t rst38_addr = memory_->read_bank(0, 0x0039) | (memory_->read_bank(0, 0x003A) << 8);
    if (rst38_op == 0xC3) {
        for (int bank = 1; bank < 8; bank++) {
            memory_->write_bank(bank, 0x0038, rst38_op);
            memory_->write_bank(bank, 0x0039, rst38_addr & 0xFF);
            memory_->write_bank(bank, 0x003A, (rst38_addr >> 8) & 0xFF);
        }
    }

    // Patch unpatched XIOS stubs in XDOS if needed
    uint16_t xiosjmp = xios_jmp_tbl_base * 256;
    int jmp_zero_count = 0;

    for (uint16_t addr = xdos_base * 256; addr < resbdos_base * 256; addr++) {
        uint8_t op = memory_->read_common(addr);
        if (op == 0xC3) {
            uint16_t target = memory_->read_common(addr + 1) |
                             (memory_->read_common(addr + 2) << 8);
            if (target == 0) {
                jmp_zero_count++;
            }
        }
    }

    if (jmp_zero_count > 0) {
        // Find the first unpatched stub and patch all 8 XIOS stubs
        uint16_t first_stub = 0;
        for (uint16_t addr = xdos_base * 256; addr < resbdos_base * 256; addr++) {
            uint8_t op = memory_->read_common(addr);
            if (op == 0xC3) {
                uint16_t target = memory_->read_common(addr + 1) |
                                 (memory_->read_common(addr + 2) << 8);
                if (target == 0) {
                    first_stub = addr;
                    break;
                }
            }
        }

        if (first_stub != 0) {
            const uint8_t offsets[] = {0x33, 0x36, 0x39, 0x3C, 0x3F, 0x42, 0x45, 0x48};
            for (int i = 0; i < 8; i++) {
                uint16_t stub_addr = first_stub + (i * 3);
                uint16_t target = xiosjmp + offsets[i];
                uint8_t op = memory_->read_common(stub_addr);
                uint16_t old_target = memory_->read_common(stub_addr + 1) |
                                     (memory_->read_common(stub_addr + 2) << 8);
                if (op == 0xC3 && old_target == 0) {
                    memory_->write_common(stub_addr + 1, target & 0xFF);
                    memory_->write_common(stub_addr + 2, (target >> 8) & 0xFF);
                }
            }
        }
    }

    return true;
}

void Z80Runner::start() {
    if (running_.load()) return;

    stop_requested_.store(false);
    timed_out_.store(false);
    running_.store(true);
    start_time_ = std::chrono::steady_clock::now();
    next_tick_ = start_time_;
    tick_count_ = 0;
    instruction_count_.store(0);

    thread_ = std::thread(&Z80Runner::thread_func, this);
}

void Z80Runner::stop() {
    if (!running_.load()) return;

    stop_requested_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    running_.store(false);
}

void Z80Runner::set_xios_base(uint16_t base) {
    if (xios_) {
        xios_->set_base(base);
    }
}

void Z80Runner::enable_interrupts(bool enable) {
    if (cpu_) {
        cpu_->regs.IFF1 = enable ? 1 : 0;
        cpu_->regs.IFF2 = enable ? 1 : 0;
    }
}

uint64_t Z80Runner::cycles() const {
    return cpu_ ? cpu_->cycles : 0;
}

bool Z80Runner::run_polled() {
    // Single-threaded polling mode - runs a batch of instructions
    // Returns false when should exit (shutdown or timeout)

    if (stop_requested_.load()) return false;

    // Initialize timing on first call
    static bool first_call = true;
    if (first_call) {
        start_time_ = std::chrono::steady_clock::now();
        next_tick_ = start_time_;
        tick_count_ = 0;
        instruction_count_.store(0);
        running_.store(true);
        first_call = false;
    }

    // Run a batch of instructions
    for (int batch = 0; batch < 10000; batch++) {
        // Check for timeout
        auto now = std::chrono::steady_clock::now();
        if (timeout_seconds_ > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
            if (elapsed >= timeout_seconds_) {
                timed_out_.store(true);
                return false;
            }
        }

        // Check for timer interrupt (60Hz tick)
        if (now >= next_tick_) {
            next_tick_ = now + TICK_INTERVAL;

            // Auto-start clock after boot completes (5M instructions)
            static bool auto_started = false;
            if (!auto_started && instruction_count_.load() > 5000000) {
                xios_->start_clock();
                auto_started = true;
            }

            // Request tick interrupt if clock is enabled
            if (xios_->clock_enabled()) {
                static uint64_t last_int_cycles = 0;
                constexpr uint64_t MIN_CYCLES_BETWEEN_INTERRUPTS = 66667;

                uint64_t current_cycles = cpu_->cycles;
                uint64_t cycles_since_last = current_cycles - last_int_cycles;

                if (cpu_->regs.IFF1 && cycles_since_last >= MIN_CYCLES_BETWEEN_INTERRUPTS) {
                    cpu_->request_rst(7);  // RST 38H
                    last_int_cycles = current_cycles;
                }
                xios_->tick();
            }

            // Check for one-second tick
            if (++tick_count_ >= 60) {
                tick_count_ = 0;
                xios_->one_second_tick();
            }
        }

        // Handle HALT
        if (cpu_->is_halted()) {
            if (cpu_->check_interrupts()) {
                cpu_->clear_halted();
            } else {
                break;
            }
        }

        cpu_->check_interrupts();
        cpu_->execute();
        instruction_count_++;
    }

    return true;
}

void Z80Runner::thread_func() {
    while (!stop_requested_.load()) {
        // Check for timeout
        auto now = std::chrono::steady_clock::now();
        if (timeout_seconds_ > 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
            if (elapsed >= timeout_seconds_) {
                timed_out_.store(true);
                stop_requested_.store(true);
                break;
            }
        }

        // Check for timer interrupt (60Hz tick)
        if (now >= next_tick_) {
            next_tick_ = now + TICK_INTERVAL;

            // Auto-start clock after boot completes (5M instructions)
            static bool auto_started = false;
            if (!auto_started && instruction_count_.load() > 5000000) {
                xios_->start_clock();
                auto_started = true;
            }

            // Request tick interrupt if clock is enabled
            if (xios_->clock_enabled()) {
                static int tick_count_local = 0;
                tick_count_local++;
                if (tick_count_local < 10) continue;

                static uint64_t last_int_cycles = 0;
                constexpr uint64_t MIN_CYCLES_BETWEEN_INTERRUPTS = 66667;

                uint64_t current_cycles = cpu_->cycles;
                uint64_t cycles_since_last = current_cycles - last_int_cycles;

                if (cpu_->regs.IFF1 && cycles_since_last >= MIN_CYCLES_BETWEEN_INTERRUPTS) {
                    cpu_->request_rst(7);  // RST 38H
                    last_int_cycles = current_cycles;
                }
                xios_->tick();
            }

            // Check for one-second tick
            if (++tick_count_ >= 60) {
                tick_count_ = 0;
                xios_->one_second_tick();
            }
        }

        // Handle HALT
        if (cpu_->is_halted()) {
            if (cpu_->check_interrupts()) {
                cpu_->clear_halted();
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
        }

        cpu_->check_interrupts();
        cpu_->execute();
        instruction_count_++;
    }
}

