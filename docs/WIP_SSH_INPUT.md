# WIP: SSH Input Not Working

## Status
SSH connects and displays output correctly, but input commands (like `stat`) get no response.

## What Works
- SSH connection establishes
- MP/M II banner and prompt display
- SSH input is received by emulator: `[SSH IN] Got 5 bytes: 0x73 0x74 0x61 0x74 0xd` ("stat\r")
- IFF1 toggles correctly during boot (EI/DI pairs for critical sections)

## The Problem
IFF1 gets stuck at 0 around instruction ~900K-1.1M and never recovers. Without interrupts, the console polling never happens and input is never processed.

## Key Findings

1. **Low common memory fix applied**: Page 0 (0x0000-0x00FF) is now common memory so RST 38H vector works across all banks.

2. **Removed C++ interrupt manipulation**: Following user guidance, removed all direct IFF1/IFF2 writes from C++. Assembly handles interrupts via EI instruction.

3. **IDLE entry is DB 0,0,0**: Matches SIMH approach - forces internal dispatch at idle.

4. **Removed SETPREEMP port calls**: Preempted flag is now only in assembly memory, not tracked in C++.

5. **IFF1 trace shows**: Early boot has normal EI/DI cycling, but around ~1M instructions, IFF1 goes to 0 and stays there permanently.

## Files Changed
- `src/banked_mem.cpp` - Low common area for page 0
- `include/banked_mem.h` - LOW_COMMON_SIZE constant
- `src/xios.cpp` - Removed IFF1/IFF2/preempted_ manipulation
- `include/xios.h` - Removed preempted_ member
- `asm/bnkxios.asm` - Removed SETPREEMP port calls, IDLE is DB 0,0,0
- `src/z80_runner.cpp` - Debug tracing for IFF1 (temporary)

## Next Steps
1. Find exact instruction/PC where IFF1 gets stuck at 0
2. Determine if it's a DI without matching EI, or an interrupt that never returns
3. Check if timer interrupts are being requested but not serviced
4. Compare interrupt handler flow with SIMH/P112 implementations

## Reference
- P112 MP/M II XIOS: https://github.com/hperaza/P112-mpmII
- Their IDLE is also DB 0,0,0 to force internal dispatch
