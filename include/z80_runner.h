// z80_runner.h - Z80 CPU emulation runner
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef Z80_RUNNER_H
#define Z80_RUNNER_H

#include <thread>
#include <atomic>
#include <chrono>
#include <memory>

class qkz80;
class MpmCpu;
class BankedMemory;
class XIOS;

// Z80 emulator runner - runs the CPU and handles timer interrupts
class Z80Runner {
public:
    Z80Runner();
    ~Z80Runner();

    // Initialize with memory and load boot code
    bool init(const std::string& boot_image);

    // Boot from disk sector 0 (cold boot loader)
    // Reads sector 0 from drive A into 0x0000 and starts execution there
    bool boot_from_disk();

    // Load MPM.SYS directly (bypasses MPMLDR)
    // Returns true on success, sets entry_point to xdos_base*256
    bool load_mpm_sys(const std::string& mpm_sys_path);

    // Start/stop the CPU thread
    void start();
    void stop();

    // Single-threaded polling mode - call this in a loop instead of start()
    // Returns false when should exit (shutdown requested or timeout)
    bool run_polled();

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

    // Timeout for debugging
    void set_timeout(int seconds) { timeout_seconds_ = seconds; }
    bool timed_out() const { return timed_out_.load(); }

private:
    void thread_func();

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

    // Timeout
    int timeout_seconds_ = 0;  // 0 = no timeout
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<bool> timed_out_{false};

    // System configuration (from SYSDAT)
    int num_consoles_ = 8;   // Number of consoles (from SYSDAT offset 1)
    int num_mem_segs_ = 8;   // Number of memory segments (from SYSDAT offset 15)

    // Debug addresses
    uint16_t tickn_addr_ = 0;  // Address of TICKN flag in BNKXIOS
};

#endif // Z80_RUNNER_H
