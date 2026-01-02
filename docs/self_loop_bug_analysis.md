# Self-Loop Bug Analysis

## Symptom
The scheduler enters an infinite loop when the Tick process's link field points to itself (self-loop at 0xED98).

## Current "Fix"
The code in `mpm_cpu.cpp` intercepts writes at PC=0xdc37 when BC=DE=Tick address and skips to RET, preventing the self-loop write.

**This is a workaround, not a root cause fix.**

## Root Cause Analysis

### How insert_process works (from DSPTCH.ASM)

```
Entry: BC = address of list root pointer, DE = process descriptor to insert

Loop:
  1. HL = [BC] (read current list element)
  2. If HL == 0 or pd.priority < current.priority, insert here
  3. Otherwise: BC = HL (advance to next element), goto Loop

Insert:
  1. pd.link = HL (new process points to rest of list)
  2. [BC] = DE (previous element points to new process)
```

The algorithm is correct. BC starts as the address of `rlr` variable, then becomes the address of each process descriptor's link field as it traverses.

### Why BC == DE causes self-loop

If the process being inserted (DE) is **already in the list**, then:
1. BC will eventually equal DE (pointing to the process's own link field)
2. The insert writes: `[BC] = DE` → `[DE] = DE` → **self-loop**

### Why Tick might be re-inserted

The flag mechanism should prevent this:
1. `FLAGWAIT(1)`: Tick removed from RLR, `sys_flag[1] = Tick_address`
2. `FLAGSET(1)`: Sees waiting process, puts Tick on DRL, `sys_flag[1] = 0xFFFF`
3. Dispatcher: Moves Tick from DRL to RLR
4. If interrupt fires again before Tick calls FLAGWAIT:
   - `FLAGSET(1)` sees `sys_flag[1] = 0xFFFF`, sets to `0xFFFE`
   - **No insertion** - flag over-run, returns error

### Possible emulator bugs

1. **Interrupt timing**: Delivering interrupts faster than real hardware
2. **Flag state corruption**: Not correctly maintaining `sys_flag[]` states
3. **Double dispatch**: Calling dispatcher when DRL shouldn't have processes
4. **IFF handling**: The "force-enabling interrupts" code at line 873 is suspicious

## Why SIMH and real systems work

Real MP/M II hardware and SIMH:
- Have correct interrupt timing (60Hz, not faster)
- Properly gate interrupts via IFF1/IFF2
- Don't have race conditions in flag state

## Recommended investigation

1. Add tracing to `sys_flag[]` array to verify state transitions
2. Verify FLAGSET return value (0 = success, 0xFF = over-run)
3. Check if DRL is being populated when it shouldn't be
4. Review interrupt delivery timing vs. IFF state

## References

- `/mpm2_external/mpm2src/NUCLEUS/DSPTCH.ASM` - Dispatcher and insert_process
- `/mpm2_external/mpm2src/NUCLEUS/FLAG.ASM` - FLAGWAIT and FLAGSET
- `/mpm2_external/mpm2src/NUCLEUS/TICK.ASM` - Tick process
- `/mpm2_external/mpm2src/CONTROL/RESXIOS.ASM` - Interrupt handler (INTHND)
