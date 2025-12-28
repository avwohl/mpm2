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

        // Monitor SP near FC00
        static uint16_t last_sp = 0;
        uint16_t cur_sp = cpu_->regs.SP.get_pair16();
        static int sp_fc_trace = 0;
        static int sp_at_fc00_count = 0;
        if (cur_sp != last_sp && cur_sp >= 0xFC00 && cur_sp <= 0xFD10 && sp_fc_trace++ < 5) {
            std::cerr << "[SP CHANGE] was 0x" << std::hex << last_sp
                      << " now 0x" << cur_sp << " PC=0x" << pc << std::dec << "\n";
        }
        // Trace next 10 instructions after SP becomes FC00
        if (cur_sp == 0xFC00 && last_sp != 0xFC00) {
            sp_at_fc00_count = 10;
            std::cerr << "[SP=FC00 START] PC=0x" << std::hex << pc << std::dec << "\n";
        }
        if (sp_at_fc00_count > 0) {
            uint8_t b0 = memory_->fetch_mem(pc);
            uint8_t b1 = memory_->fetch_mem(pc+1);
            uint8_t b2 = memory_->fetch_mem(pc+2);
            std::cerr << "[SP=FC00 TRACE] PC=0x" << std::hex << pc
                      << " instr=" << (int)b0 << " " << (int)b1 << " " << (int)b2
                      << " SP=0x" << cur_sp
                      << std::dec << "\n";
            sp_at_fc00_count--;
        }
        last_sp = cur_sp;

        // Monitor FC00 byte for changes
        static uint8_t last_fc00 = 0;
        static int fc00_changes = 0;
        uint8_t cur_fc00 = memory_->fetch_mem(0xFC00);
        if (cur_fc00 != last_fc00 && fc00_changes++ < 10) {
            std::cerr << "[FC00 CHANGE] was 0x" << std::hex << (int)last_fc00
                      << " now 0x" << (int)cur_fc00
                      << " PC=0x" << pc;
            // Dump instruction at PC
            uint8_t b0 = memory_->fetch_mem(pc);
            uint8_t b1 = memory_->fetch_mem(pc+1);
            uint8_t b2 = memory_->fetch_mem(pc+2);
            std::cerr << " instr=" << (int)b0 << " " << (int)b1 << " " << (int)b2;
            // Show regs
            std::cerr << " HL=" << cpu_->regs.HL.get_pair16()
                      << " DE=" << cpu_->regs.DE.get_pair16()
                      << " BC=" << cpu_->regs.BC.get_pair16()
                      << " SP=" << cpu_->regs.SP.get_pair16()
                      << std::dec << "\n";
            // Dump FC00-FC0F
            std::cerr << "  FC00: ";
            for (int i = 0; i < 16; i++) {
                std::cerr << std::hex << std::setw(2) << std::setfill('0')
                          << (int)memory_->fetch_mem(0xFC00 + i) << " ";
            }
            std::cerr << std::dec << "\n";
            last_fc00 = cur_fc00;
        }

        // XIOSJMP interception: redirect FC00-FC5A to emulator's XIOS handler
        // Both XIOSJMP (FC00) and BNKXIOS (CC00/CD00) get corrupted during MP/M II init.
        // Handle XIOS calls directly via the emulator.
        if (pc >= 0xFC00 && pc < 0xFC5D) {
            uint16_t offset = pc - 0xFC00;
            // Valid XIOS entry points are at 3-byte intervals
            if (offset % 3 == 0) {
                static int xios_intercept_count = 0;
                if (xios_intercept_count++ < 30) {
                    std::cerr << "[XIOSJMP->EMU] offset=0x" << std::hex << offset << std::dec << "\n";
                }
                // Call emulator's XIOS handler via port dispatch mechanism
                xios_->handle_port_dispatch(offset);
                // Pop return address from stack and return
                uint16_t sp = cpu_->regs.SP.get_pair16();
                uint8_t lo = memory_->fetch_mem(sp);
                uint8_t hi = memory_->fetch_mem(sp + 1);
                uint16_t ret_addr = (hi << 8) | lo;
                cpu_->regs.SP.set_pair16(sp + 2);
                cpu_->regs.PC.set_pair16(ret_addr);

                // Debug: show return address for BOOT calls
                if (offset == 0 && xios_intercept_count < 35) {
                    uint8_t b0 = memory_->fetch_mem(ret_addr);
                    uint8_t b1 = memory_->fetch_mem(ret_addr + 1);
                    uint8_t b2 = memory_->fetch_mem(ret_addr + 2);
                    std::cerr << "[BOOT RET] bank=" << (int)memory_->current_bank()
                              << " sp=" << std::hex << sp
                              << " ret_addr=" << ret_addr
                              << " bytes=" << (int)b0 << " " << (int)b1 << " " << (int)b2
                              << std::dec << "\n";
                }

                instruction_count_++;
                continue;  // Skip normal instruction execution
            }
        }

        // Check for HALT instruction (0x76) - handle specially for MP/M
        uint8_t opcode = memory_->fetch_mem(pc);
        // qkz80 library calls exit() on HALT, but MP/M uses HALT in idle loop
        if (opcode == 0x76) {
            // HALT - wait for interrupt
            static int halt_count = 0;
            if (halt_count++ < 5) {
                std::cerr << "[Z80] HALT at PC=0x" << std::hex << pc
                          << " clock_enabled=" << xios_->clock_enabled()
                          << " IFF1=" << (int)cpu_->regs.IFF1 << std::dec << "\n";
            }

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
