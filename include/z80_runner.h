// z80_runner.h - Z80 CPU emulation runner
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef Z80_RUNNER_H
#define Z80_RUNNER_H

#include <atomic>
#include <chrono>
#include <memory>

class qkz80;
class MpmCpu;
class BankedMemory;
class XIOS;

// Z80 emulator runner - runs the CPU and handles timer interrupts
// Single-threaded polling mode only
class Z80Runner {
public:
    Z80Runner();
    ~Z80Runner();

    // Boot from disk sector 0 (cold boot loader)
    // Reads sector 0 from drive A into 0x0000 and starts execution there
    bool boot_from_disk();

    // Run one batch of instructions (call in main loop)
    // Returns false when should exit (shutdown requested or timeout)
    bool run_polled();

    // Request shutdown
    void request_stop() { stop_requested_.store(true); }

    // Check if running
    bool is_running() const { return running_.load(); }

    // Access to components
    MpmCpu* cpu() { return cpu_.get(); }
    BankedMemory* memory() { return memory_.get(); }
    XIOS* xios() { return xios_.get(); }

    // Statistics
    uint64_t cycles() const;
    uint64_t instructions() const { return instruction_count_.load(); }

    // Timeout for debugging
    void set_timeout(int seconds) { timeout_seconds_ = seconds; }
    bool timed_out() const { return timed_out_.load(); }

private:
    std::unique_ptr<MpmCpu> cpu_;
    std::unique_ptr<BankedMemory> memory_;
    std::unique_ptr<XIOS> xios_;

    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;

    // Timing
    std::chrono::steady_clock::time_point next_tick_;
    static constexpr auto TICK_INTERVAL = std::chrono::microseconds(16667);  // 60Hz

    // Counters
    std::atomic<uint64_t> instruction_count_;

    // Timeout
    int timeout_seconds_ = 0;  // 0 = no timeout
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<bool> timed_out_{false};
};

#endif // Z80_RUNNER_H
