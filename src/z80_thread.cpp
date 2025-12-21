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
    std::cerr << "[Z80] init() called with boot_image='" << boot_image << "'" << std::endl;
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

        // Debug: verify FF4E loaded correctly
        std::cerr << "[Z80] After load, FF4E=" << std::hex
                  << (int)memory_->fetch_mem(0xFF4E) << " "
                  << (int)memory_->fetch_mem(0xFF4F) << " "
                  << (int)memory_->fetch_mem(0xFF50) << std::dec << std::endl;

        // Note: BNKXIOS pre-loading disabled - using patched NUCLEUS MPM.SYS
        // which already contains the BNKXIOS forwarding stub at BA00

        // Set PC to 0x0100 where MPMLDR starts
        cpu_->regs.PC.set_pair16(0x0100);

        // Set up stack pointer in high memory (will be reset by MPMLDR)
        cpu_->regs.SP.set_pair16(0x0080);
        std::cerr << "[Z80] Boot image loaded, PC=0x0100" << std::endl;
    }

    std::cerr << "[Z80] init() returning true" << std::endl;
    return true;
}

void Z80Thread::start() {
    std::cerr << "[Z80] start() called" << std::endl;
    if (running_.load()) return;

    stop_requested_.store(false);
    running_.store(true);
    next_tick_ = std::chrono::steady_clock::now();
    tick_count_ = 0;
    instruction_count_.store(0);

    std::cerr << "[Z80] Creating thread..." << std::endl;
    thread_ = std::thread(&Z80Thread::thread_func, this);
    std::cerr << "[Z80] Thread created" << std::endl;
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
    std::cerr << "[Z80 Thread] Starting execution at PC=0x"
              << std::hex << cpu_->regs.PC.get_pair16() << std::dec << std::endl;

    // Debug: verify memory is accessible
    uint16_t test_pc = cpu_->regs.PC.get_pair16();
    std::cerr << "[Z80 Thread] Testing memory at PC=0x" << std::hex << test_pc << std::endl;
    uint8_t byte0 = memory_->fetch_mem(test_pc);
    uint8_t byte1 = memory_->fetch_mem(test_pc + 1);
    uint8_t byte2 = memory_->fetch_mem(test_pc + 2);
    std::cerr << "[Z80 Thread] Memory at PC: " << std::hex
              << (int)byte0 << " " << (int)byte1 << " " << (int)byte2 << std::dec << std::endl;

    while (!stop_requested_.load()) {
        // Check for timer interrupt
        auto now = std::chrono::steady_clock::now();
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

        // NOTE: PC-based XIOS interception has been removed.
        // All XIOS calls now use I/O port dispatch via port 0xE0.
        // The Z80 XIOS code does: LD B, func; OUT (0xE0), A; RET
        // and the emulator's MpmCpu::port_out() handles the dispatch.

        // Boot tracing - trace all jumps into high memory
        static bool booted = false;
        static int pre_boot_trace = 0;
        static int post_boot_trace = 0;
        static uint16_t last_pc = 0;

        // Trace jumps from low memory (loader) to high memory (nucleus)
        if (!booted && last_pc < 0x8000 && pc >= 0x8000) {
            fprintf(stderr, "\n[BOOT JUMP] from %04X to %04X\n", last_pc, pc);
            // Dump memory at the jump destination
            fprintf(stderr, "[BOOT JUMP] Code at %04X: ", pc);
            for (int i = 0; i < 16; i++) {
                fprintf(stderr, "%02X ", memory_->fetch_mem(pc + i));
            }
            fprintf(stderr, "\n");
        }

        // Trace when we first reach the nucleus area (CE00+)
        if (!booted && pc >= 0xCE00) {
            booted = true;
            fprintf(stderr, "\n[BOOT] System reached high memory at 0x%04X (came from 0x%04X)\n", pc, last_pc);
            // Dump memory map around BNKXIOS (CD00-CDFF)
            fprintf(stderr, "[BOOT] BNKXIOS area (CD00-CD30):\n");
            for (uint16_t addr = 0xCD00; addr < 0xCD30; addr += 16) {
                fprintf(stderr, "  %04X: ", addr);
                for (int i = 0; i < 16; i++) {
                    fprintf(stderr, "%02X ", memory_->fetch_mem(addr + i));
                }
                fprintf(stderr, "\n");
            }
        }

        // Trace PC values in BNKXIOS range (CD00-CDFF)
        if (pc >= 0xCD00 && pc < 0xCE00 && pre_boot_trace++ < 20) {
            fprintf(stderr, "[BNKXIOS] PC=%04X op=%02X\n", pc, memory_->fetch_mem(pc));
        }

        // Trace PC after boot to understand where we go
        if (booted && post_boot_trace++ < 50) {
            uint8_t op = memory_->fetch_mem(pc);
            fprintf(stderr, "[POST-BOOT %d] PC=%04X op=%02X\n", post_boot_trace, pc, op);
        }

        last_pc = pc;

        // Check for HALT instruction (0x76) - handle specially for MP/M
        uint8_t opcode = memory_->fetch_mem(pc);
        // qkz80 library calls exit() on HALT, but MP/M uses HALT in idle loop
        if (opcode == 0x76) {
            // HALT - wait for interrupt
            // Advance PC past HALT
            cpu_->regs.PC.set_pair16(pc + 1);
            instruction_count_++;

            // Wait for timer interrupt or stop request
            while (!stop_requested_.load() && !xios_->clock_enabled()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
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
