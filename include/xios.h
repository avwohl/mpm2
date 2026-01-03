// xios.h - MP/M II Extended I/O System
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef XIOS_H
#define XIOS_H

#include <cstdint>
#include <atomic>

class qkz80;
class BankedMemory;

// XIOS jump table offsets (from BIOS base)
// Standard BIOS entries (00H-30H)
constexpr uint8_t XIOS_BOOT      = 0x00;  // Cold boot
constexpr uint8_t XIOS_WBOOT     = 0x03;  // Warm boot
constexpr uint8_t XIOS_CONST     = 0x06;  // Console status
constexpr uint8_t XIOS_CONIN     = 0x09;  // Console input
constexpr uint8_t XIOS_CONOUT    = 0x0C;  // Console output
constexpr uint8_t XIOS_LIST      = 0x0F;  // List output
constexpr uint8_t XIOS_PUNCH     = 0x12;  // Punch output
constexpr uint8_t XIOS_READER    = 0x15;  // Reader input
constexpr uint8_t XIOS_HOME      = 0x18;  // Home disk
constexpr uint8_t XIOS_SELDSK    = 0x1B;  // Select disk
constexpr uint8_t XIOS_SETTRK    = 0x1E;  // Set track
constexpr uint8_t XIOS_SETSEC    = 0x21;  // Set sector
constexpr uint8_t XIOS_SETDMA    = 0x24;  // Set DMA address
constexpr uint8_t XIOS_READ      = 0x27;  // Read sector
constexpr uint8_t XIOS_WRITE     = 0x2A;  // Write sector
constexpr uint8_t XIOS_LISTST    = 0x2D;  // List status
constexpr uint8_t XIOS_SECTRAN   = 0x30;  // Sector translate

// Extended XIOS entries (33H-48H)
constexpr uint8_t XIOS_SELMEMORY   = 0x33;  // Select memory bank
constexpr uint8_t XIOS_POLLDEVICE  = 0x36;  // Poll device
constexpr uint8_t XIOS_STARTCLOCK  = 0x39;  // Start system clock
constexpr uint8_t XIOS_STOPCLOCK   = 0x3C;  // Stop system clock
constexpr uint8_t XIOS_EXITREGION  = 0x3F;  // Exit critical region
constexpr uint8_t XIOS_MAXCONSOLE  = 0x42;  // Maximum console number
constexpr uint8_t XIOS_SYSTEMINIT  = 0x45;  // System initialization
constexpr uint8_t XIOS_IDLE        = 0x48;  // Idle procedure

// Commonbase entries (at XIOS base + 0x4B, 3-byte aligned after IDLE at 0x48)
// These are patched by GENSYS and called by XDOS/BNKBDOS
// Note: Entries are spaced 3 bytes apart (JP instructions)
constexpr uint8_t XIOS_COMMONBASE  = 0x4B;  // Entry that returns commonbase address
constexpr uint8_t XIOS_SWTUSER     = 0x4E;  // Switch to user bank (first commonbase entry)
constexpr uint8_t XIOS_SWTSYS      = 0x51;  // Switch to system bank
constexpr uint8_t XIOS_PDISP       = 0x54;  // Process dispatcher
constexpr uint8_t XIOS_XDOSENT     = 0x57;  // XDOS entry
constexpr uint8_t XIOS_SYSDAT      = 0x5A;  // System data pointer (2-byte DW)

// MP/M II flags (set by interrupt handlers)
constexpr uint8_t FLAG_TICK     = 1;   // System tick (16.67ms)
constexpr uint8_t FLAG_SECOND   = 2;   // One-second flag
constexpr uint8_t FLAG_DISK     = 5;   // Disk operation complete

// XIOS context - maintains state for XIOS calls
class XIOS {
public:
    XIOS(qkz80* cpu, BankedMemory* mem);

    // Set XIOS base address (jump table location)
    void set_base(uint16_t base) { xios_base_ = base; }
    uint16_t base() const { return xios_base_; }

    // Handle XIOS call via I/O port dispatch
    // func = function offset (matches jump table: 0x00=BOOT, 0x06=CONST, etc.)
    // Called when Z80 executes OUT (0xE0), A with B=function
    void handle_port_dispatch(uint8_t func);


    // Timer tick - called from interrupt handler
    void tick();

    // One-second tick
    void one_second_tick();

    // Clock control (STARTCLOCK/STOPCLOCK)
    bool clock_enabled() const { return tick_enabled_.load(); }
    void start_clock() { tick_enabled_.store(true); }

    // PREEMPT flag for interrupt handling
    bool is_preempted() const { return preempted_.load(); }
    void set_preempted(bool p) { preempted_.store(p); }

    // Update DMA target bank (called when bank switching via port 0xE1)
    void update_dma_bank(uint8_t bank) { if (bank != 0) dma_bank_ = bank; }

private:
    // BIOS-compatible entries
    void do_boot();
    void do_wboot();
    void do_const();
    void do_conin();
    void do_conout();
    void do_list();
    void do_punch();
    void do_reader();
    void do_home();
    void do_seldsk();
    void do_settrk();
    void do_setsec();
    void do_setdma();
    void do_read();
    void do_write();
    void do_listst();
    void do_sectran();

    // Extended XIOS entries
    void do_selmemory();
    void do_polldevice();
    void do_startclock();
    void do_stopclock();
    void do_exitregion();
    void do_maxconsole();
    void do_systeminit();
    void do_idle();

    // Commonbase entries
    void do_swtuser();   // Switch to user bank
    void do_swtsys();    // Switch to system bank
    void do_pdisp();     // Process dispatcher
    void do_xdosent();   // XDOS entry point
    void do_sysdat();    // System data pointer

    // Simulate RET instruction
    void do_ret();

    qkz80* cpu_;
    BankedMemory* mem_;
    uint16_t xios_base_;

    // Disk state
    uint8_t current_disk_;
    uint16_t current_track_;
    uint16_t current_sector_;
    uint16_t dma_addr_;
    uint8_t dma_bank_;          // Target bank for DMA to banked addresses

    // Clock control
    std::atomic<bool> tick_enabled_;
    std::atomic<bool> preempted_;

    // Skip RET flag for I/O port dispatch
    bool skip_ret_ = false;
};

#endif // XIOS_H
