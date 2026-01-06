# SSH/Console Input Investigation - Work In Progress

## Problem Statement

Console input (both SSH and local) only works for the startup command. Subsequent commands entered via SSH or local console are queued but never processed by MP/M II.

## Symptoms

1. SSH connects successfully, characters are queued to console's input_queue
2. Startup command (from `$3$.SUP`) executes correctly during boot
3. After boot completes and prompt appears, new input is never processed
4. System appears stuck but is actually running (tick interrupts continue)

## Root Cause Analysis

### POLLDEVICE Stops Being Called

During boot, POLLDEVICE is called ~100 times from XDOS at address 0xde10. After boot completes, POLLDEVICE is **never called again**.

```
[POLL-ALL] #1 dev=7 from=0xde10
[POLL-ALL] #2 dev=7 from=0xde10
...
[POLL7] #100 total=100
(no more POLL calls after this)
```

### Dispatcher Poll Loop Never Reached

The MP/M II dispatcher (DSPTCH.ASM) has a poll loop at label @7 that should:
1. Walk the poll list (PLR)
2. Call POLLDEVICE for each process's device
3. Move ready processes to the ready list (RLR)

This loop is **never reached** after boot. The dispatcher flow goes:
- @17 (status 9 = dispatch) -> @6 -> should go to @7
- But something prevents @7 from executing

### IFF1 Stays at 0

After boot, IFF1 (interrupt flag) stays at 0, meaning:
- Timer interrupts (RST 38H) are not delivered
- The dispatcher is never triggered by timer
- System runs but nothing gets scheduled

## Current State of Workarounds

### POLL-FIX (z80_thread.cpp ~line 1019)

Checks poll list every second. If a process is waiting for console input and input is available:
1. Moves process from poll list to ready list
2. Clears indisp flag (at 0xeec3)
3. Sets IFF1=1 and requests RST 38H interrupt

**Result**: Works for ONE command, then corrupts MP/M state:
- PLR becomes 0 and stays 0
- Process never goes back on poll list after command finishes

### IFF-FIX (z80_thread.cpp ~line 1081)

If IFF1=0 for more than 5 consecutive seconds (after sec 10), forces IFF1=1.

**Result**: Keeps timer interrupts working but doesn't solve the poll list issue.

## Key Memory Locations

| Address | Description |
|---------|-------------|
| 0xFFFC-0xFFFD | DATAPG pointer |
| DATAPG+5,6 | RLR (Ready List Root) |
| DATAPG+11,12 | PLR (Poll List Root) |
| 0xeec3 | indisp flag (0xFF = in dispatch) |
| 0xfe00 | Tmp3 process descriptor |
| 0xfe00+2 | Tmp3 status (0=ready, 9=dispatch) |
| 0xfe00+0x10 | Tmp3 poll device (7 = console 3 input) |

## Process Descriptor Layout

```
Offset  Size  Field
0-1     2     Link (next in list)
2       1     Status (0=ready, 9=dispatch, etc.)
3       1     Priority
4-5     2     SP save
6-13    8     Name (8 chars)
...
0x10    1     Device number for polling
```

## What We Know Works

1. Console input queue (`con->input_queue()`) - characters are queued correctly
2. POLLDEVICE implementation - returns 0xFF when queue has data
3. Startup command execution - TMP processes `$3$.SUP` during boot
4. Timer tick generation - 60Hz ticks are generated correctly
5. Interrupt delivery - RST 38H works when IFF1=1

## What's Broken

1. Dispatcher poll loop (@7) never executes after boot
2. After POLL-FIX runs once, process never returns to poll list
3. IFF1 stays at 0 after boot (something disables interrupts and never re-enables)

## Files Modified

- `src/z80_thread.cpp` - POLL-FIX and IFF-FIX workarounds
- `src/xios.cpp` - POLLDEVICE debug logging
- `src/console.cpp` - const_status debug logging

## Next Steps to Investigate

1. **Why doesn't dispatcher reach @7?**
   - Trace DSPTCH.ASM flow after boot
   - Check what status values processes have
   - Understand @17 -> @6 -> @7 transition

2. **Why does process not return to poll list?**
   - After DIR finishes, TMP calls CONIN
   - CONIN should call XDOS which calls POLLDEVICE
   - If not ready, should go on poll list
   - Trace this flow after our POLL-FIX manipulation

3. **Alternative workaround approaches**
   - Instead of moving process to ready list, just make POLLDEVICE return "ready"
   - Periodically call dispatcher directly from C++
   - Patch XDOS to fix the poll loop issue

4. **Compare with SIMH**
   - SIMH comment says: "Console polling from interrupt was causing XDOS reentrancy issues"
   - How does SIMH solve this?

## Test Commands

```bash
# Start emulator with SSH
./mpm2_emu -s disks/mpm.sys -d A:disks/mpm2_system.img -t 60 -p 2222

# Test SSH input
echo 'DIR' | ssh -T -p 2222 user@localhost

# Start with local console
./mpm2_emu -l -s disks/mpm.sys -d A:disks/mpm2_system.img -t 30
```

## Debug Output to Watch For

```
[POLL-FIX] Console 3 has input, waking process at 0xfe00
[POLLFIX-CHK] #N PLR=0xfe00   <- Process on poll list (good)
[POLLFIX-CHK] #N PLR=0x0      <- Poll list empty (bad after fix)
[IFF-FIX] #N IFF1=0 for M ticks, forcing IFF1=1
```

## Date

2026-01-02 (investigation ongoing)
