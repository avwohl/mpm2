// z80_thread.h - Z80 CPU emulation thread
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef Z80_THREAD_H
#define Z80_THREAD_H

#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

class qkz80;
class MpmCpu;
class BankedMemory;
class XIOS;

// Z80 emulator thread - runs the CPU and handles timer interrupts
class Z80Thread {
public:
    Z80Thread();
    ~Z80Thread();

    // Initialize with memory and load boot code
    bool init(const std::string& boot_image);

    // Start/stop the CPU thread
    void start();
    void stop();

    // Check if running
    bool is_running() const { return running_.load(); }

    // Access to components
    MpmCpu* cpu() { return cpu_.get(); }
    BankedMemory* memory() { return memory_.get(); }
    XIOS* xios() { return xios_.get(); }

    // Set XIOS base address
    void set_xios_base(uint16_t base);

    // Interrupt control
    void enable_interrupts(bool enable);

    // Statistics
    uint64_t cycles() const;
    uint64_t instructions() const { return instruction_count_.load(); }

private:
    void thread_func();

    // Timer interrupt delivery
    void deliver_tick_interrupt();

    std::unique_ptr<MpmCpu> cpu_;
    std::unique_ptr<BankedMemory> memory_;
    std::unique_ptr<XIOS> xios_;

    std::thread thread_;
    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;

    // Timing
    std::chrono::steady_clock::time_point next_tick_;
    static constexpr auto TICK_INTERVAL = std::chrono::microseconds(16667);  // 60Hz

    // Counters
    std::atomic<uint64_t> instruction_count_;
    int tick_count_;  // Counts to 60 for one-second flag
};

#endif // Z80_THREAD_H
