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

    // Read sector 0 into memory at 0x0000
    // For hd1k format, sector 0 is 512 bytes
    disk->set_track(0);
    disk->set_sector(0);

    uint8_t boot_sector[512];
    if (disk->read_sector(boot_sector) != 0) {
        std::cerr << "Failed to read boot sector\n";
	exit(1);
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
