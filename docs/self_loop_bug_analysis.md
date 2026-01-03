# Self-Loop Bug Analysis

## Symptom
The scheduler enters an infinite loop when the Tick process's link field points to itself (self-loop at 0xED98).

## Current Workaround
The code in `banked_mem.cpp` prevents self-loops at the memory write level by detecting when a process's link field is about to be written with its own address:

```cpp
// In store_mem(), for addresses in process descriptor range (0xED00-0xFE00):
// Block writes that would create a self-loop (link pointing to itself)
if (byte == (addr & 0xFF)) {
    uint8_t high_byte = common_[(addr + 1) - COMMON_BASE];
    if (high_byte == ((addr >> 8) & 0xFF)) {
        return;  // Block the write
    }
}
```

**This is a workaround for an unidentified emulator bug, NOT a fix for a bug in MP/M II.**

## Important: MP/M II is NOT at Fault

SIMH and real hardware run MP/M II without self-loops. The OS was commercially successful and widely deployed. The bug is in our emulator, not in MP/M II.

Reference implementations that work correctly:
- SIMH with MXIOS.MAC
- MmsCpm3 (https://github.com/durgadas311/MmsCpm3/blob/master/mpm/src/mxios.asm)
- Original hardware

## How the Self-Loop is Created

The self-loop occurs in `insert_process` (DSPTCH.ASM) when:
1. BC = address of Tick's link field (after traversing the list)
2. DE = Tick's address (the process being inserted)
3. The code writes `[BC] = DE`, which becomes `[Tick.link] = Tick`

This happens because Tick is **already in the RLR** when `insert_process` is called.

## Unknown Emulator Bug

Something in our emulator causes `insert_process` to be called with a process that's already in the list. This does NOT happen on real hardware or SIMH.

Possible areas where our emulator differs from real hardware:
1. Interrupt timing/delivery
2. IFF1/IFF2 state management
3. Cycle counting accuracy
4. Bank switch timing
5. Something else entirely unknown

### What We've Tried

1. **EI delay**: Added proper Z80 EI delay (one instruction executes after EI before interrupts are accepted)
2. **Minimum cycles between interrupts**: Enforced 66,667 cycles (~60Hz at 4MHz)
3. **Removed force-enable interrupts**: Removed code that violated DI critical sections
4. **Fixed interrupt catch-up**: Prevented rapid-fire interrupts when emulator falls behind

None of these fixed the root cause. The self-loop still occurs without the memory-level workaround.

## Key Observations

- The self-loop is created at PC=0xdc33-0xdc36 (STAX B instruction in insertprocess)
- BC = 0xed99 (high byte of Tick's link), DE = 0xed98 (Tick)
- At detection time, RLR = 0xed98 (Tick at head), Tick.status = 0
- This means Tick was NOT removed before insert_process was called

## Files Modified

- `src/banked_mem.cpp` - Self-loop prevention workaround
- `src/z80_thread.cpp` - Timing fixes (did not solve root cause)
- `../cpmemu/src/qkz80.cc` - EI delay (did not solve root cause)

## TODO: Find the Real Bug

The memory-level workaround masks the symptom but doesn't fix the emulator bug. To find the real issue:

1. Compare our interrupt handling with SIMH's implementation
2. Trace exact cycle-by-cycle behavior around the self-loop creation
3. Check if our bank switching has timing differences
4. Review IFF state transitions vs real Z80 behavior

## References

- `/mpm2_external/mpm2src/NUCLEUS/DSPTCH.ASM` - Dispatcher and insert_process
- `/mpm2_external/mpm2src/NUCLEUS/FLAG.ASM` - FLAGWAIT and FLAGSET
- `/mpm2_external/mpm2src/NUCLEUS/TICK.ASM` - Tick process
- `/mpm2_external/mpm2src/CONTROL/RESXIOS.ASM` - Interrupt handler (INTHND)
- SIMH AltairZ80 source - reference implementation that works
