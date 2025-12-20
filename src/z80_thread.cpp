// z80_thread.cpp - Z80 CPU emulation thread implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "z80_thread.h"
#include "qkz80.h"
#include "banked_mem.h"
#include "xios.h"
#include <fstream>
#include <cstring>
#include <iostream>

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

    // Create CPU with memory
    cpu_ = std::make_unique<qkz80>(memory_.get());
    cpu_->set_cpu_mode(qkz80::MODE_Z80);

    // Create XIOS
    xios_ = std::make_unique<XIOS>(cpu_.get(), memory_.get());

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

        // Load at 0x0000 in bank 0
        // For 64KB boot images, this loads the entire memory map
        memory_->load(0, 0x0000, buffer.data(), buffer.size());

        // Set PC to 0x0100 where MPMLDR starts
        cpu_->regs.PC.set_pair16(0x0100);

        // Set up stack pointer below TPA
        cpu_->regs.SP.set_pair16(0x0100);
    }

    return true;
}

void Z80Thread::start() {
    if (running_.load()) return;

    stop_requested_.store(false);
    running_.store(true);
    next_tick_ = std::chrono::steady_clock::now();
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

        if (xios_->is_xios_call(pc)) {
            if (xios_->handle_call(pc)) {
                instruction_count_++;
                continue;
            }
        }

        // Check for HALT instruction (0x76) - handle specially for MP/M
        // qkz80 library calls exit() on HALT, but MP/M uses HALT in idle loop
        uint8_t opcode = memory_->fetch_mem(pc);
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
