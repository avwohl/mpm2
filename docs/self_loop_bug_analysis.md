# Self-Loop Bug Analysis

## Symptom
The scheduler enters an infinite loop when the Tick process's link field points to itself (self-loop at 0xED98).

## Current Workaround
The code in `mpm_cpu.cpp` detects when Tick.link becomes 0xED98 (self-referential) and immediately fixes it by setting Tick.link = 0 (end of list).

```cpp
if (tick_link == TICK_ADDR) {
    mem->store_mem(TICK_ADDR, 0);
    mem->store_mem(TICK_ADDR + 1, 0);
}
```

**This is a post-hoc fix, not a root cause solution.**

## How the Self-Loop is Created

The self-loop occurs in `insert_process` (DSPTCH.ASM) when:
1. BC = address of Tick's link field (after traversing the list)
2. DE = Tick's address (the process being inserted)
3. The code writes `[BC] = DE`, which becomes `[Tick.link] = Tick`

This happens because Tick is **already in the RLR** when `insert_process` is called.

## Root Cause Investigation

### Timeline of a self-loop creation

1. Tick is at head of RLR with status=0 (ready to run)
2. Timer interrupt fires
3. PDISP sets Tick.status = 9 (Dispatch)
4. Dispatcher should remove Tick from RLR: `rlr = Tick.link`
5. Status 9 handling calls `insert_process(&rlr, Tick)`
6. Tick should be re-inserted into the now-modified RLR

**The bug**: At step 5, Tick is somehow still in RLR, causing `insert_process` to traverse the list and find Tick, creating the self-loop.

### Verified fixes (partial)

1. **Removed force-enable interrupts** - The code at line 872-878 that forced IFF=1 after 5 ticks was removed. This could have violated DI critical sections.

2. **Fixed interrupt catch-up** - Changed `next_tick_ += TICK_INTERVAL` to `next_tick_ = now + TICK_INTERVAL` to prevent rapid-fire interrupts when the emulator falls behind.

### Remaining issues

After the self-loop workaround triggers, secondary "BAD SP PREVENTED" errors occur, indicating the dispatcher is trying to resume processes with invalid stack pointers. This suggests the list corruption affects more than just Tick.

## Why SIMH and Real Systems Work

Real MP/M II hardware and SIMH:
- Have correct interrupt timing (60Hz, not faster)
- Properly gate interrupts via IFF1/IFF2
- Don't force-enable interrupts during DI critical sections
- Have correct flag state management

## Next Steps for Root Cause

1. Add tracing to `sys_flag[]` array to verify state transitions
2. Trace when processes are added to DRL vs RLR
3. Verify the dispatch removal step (step 4 above) is working correctly
4. Check if another code path is adding Tick to DRL while it's still in RLR

## Key Observations

- The self-loop is created at PC=0xdc33 (STAX B instruction in @27A)
- BC = 0xed99 (high byte of Tick's link), DE = 0xed98 (Tick)
- At detection time, RLR = 0xed98 (Tick at head), Tick.status = 0
- This means Tick was NOT removed before insert_process was called

## Files Modified

- `src/mpm_cpu.cpp` - Self-loop detection and fix
- `src/z80_thread.cpp` - Removed force-enable interrupts, fixed timing

## References

- `/mpm2_external/mpm2src/NUCLEUS/DSPTCH.ASM` - Dispatcher and insert_process
- `/mpm2_external/mpm2src/NUCLEUS/FLAG.ASM` - FLAGWAIT and FLAGSET
- `/mpm2_external/mpm2src/NUCLEUS/TICK.ASM` - Tick process
- `/mpm2_external/mpm2src/CONTROL/RESXIOS.ASM` - Interrupt handler (INTHND)
