// z80_runner.cpp - Z80 CPU emulation runner implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "z80_runner.h"
#include "mpm_cpu.h"
#include "banked_mem.h"
#include "xios.h"
#include "console.h"
#include "disk.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cstring>

Z80Runner::Z80Runner()
    : running_(false)
    , stop_requested_(false)
    , instruction_count_(0)
 {
}

Z80Runner::~Z80Runner() {
    stop_requested_.store(true);
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
	exit(1);
        return false;
    }

    std::cout << "Booting from disk A: sector 0...\n";

    // Load the entire boot image (64KB) from the first 128 sectors
    // This includes:
    //   0x0000-0x00FF: Page zero (boot vectors)
    //   0x0100-0x16FF: MPMLDR + LDRBDOS
    //   0x1700-0x1FFF: LDRBIOS
    //   0xC000-0xFFFF: Common area (includes BIOS jump table at 0xC300)
    const size_t boot_image_size = 65536;
    const size_t sector_size = disk->sector_size();
    const size_t sectors_to_read = boot_image_size / sector_size;

    std::vector<uint8_t> boot_image(boot_image_size);
    uint8_t sector_buf[512];

    for (size_t i = 0; i < sectors_to_read; i++) {
        size_t track = i / 16;  // 16 sectors per track for hd1k
        size_t sector = i % 16;
        disk->set_track(track);
        disk->set_sector(sector);

        if (disk->read_sector(sector_buf) != 0) {
            std::cerr << "Failed to read boot sector " << i << "\n";
            exit(1);
            return false;
        }

        size_t offset = i * sector_size;
        std::memcpy(&boot_image[offset], sector_buf, sector_size);
    }

    // Load into bank 0 lower memory (0x0000-0xBFFF)
    memory_->load(0, 0x0000, boot_image.data(), 0xC000);

    // Load common memory (0xC000-0xFFFF)
    for (uint16_t addr = 0xC000; addr != 0; addr++) {  // Loops until wrap
        memory_->write_common(addr, boot_image[addr]);
        if (addr == 0xFFFF) break;
    }

    std::cout << "Loaded " << boot_image_size << " bytes (boot image)\n";

    // Verify it looks like valid code (starts with DI or JP)
    uint8_t first_byte = boot_image[0];
    if (first_byte != 0xF3 && first_byte != 0xC3) {
        std::cerr << "Warning: boot sector doesn't start with DI (0xF3) or JP (0xC3)\n";
        std::cerr << "First byte: 0x" << std::hex << (int)first_byte << std::dec << "\n";
        exit(1);
    }

    // Set PC to 0x0000 to start execution at the cold boot loader
    cpu_->regs.PC.set_pair16(0x0000);

    // Set up initial stack (will be reset by the boot loader)
    cpu_->regs.SP.set_pair16(0xFFFF);

    std::cout << "Starting execution at 0x0000\n\n";

    return true;
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

	// Use RST 1 for timer, leaving RST 7 free for DDT debugger
	const int RST_INTERRUPT(1);
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
                // Always request the interrupt - CPU will process when IFF1 becomes 1
                cpu_->request_rst(RST_INTERRUPT);  // RST 38H
            }
        }

	if (cpu_->check_interrupts()) {
	  cpu_->clear_halted();
	}

	// When halted, break out of batch loop to allow SSH/console polling
	if (cpu_->is_halted()) {
	  break;
	}

        cpu_->execute();
        instruction_count_++;
    }

    return true;
}
