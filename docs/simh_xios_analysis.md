# SIMH XIOS Analysis: Why Local Console Input Fails

This document analyzes the SIMH Altair 8800 MP/M II XIOS implementation and the
MP/M II XDOS/dispatcher source code to understand why interactive console input
works in SIMH but fails in our emulator's local console mode.

## SIMH Hardware Model

SIMH emulates the Altair 8800 with **physically separate console ports**:

| Console | Status Port | Data Port |
|---------|-------------|-----------|
| 0 | 0x10 | 0x11 |
| 1 | 0x14 | 0x15 |
| 2 | 0x16 | 0x17 |
| 3 | 0x18 | 0x19 |

Each terminal connection goes to exactly one console. Input typed on "console 1's terminal" only affects console 1's status register. There is no ambiguity about which console receives input.

## SIMH CONIN Implementation

From `MPMXIOS.MAC` lines 282-296:

```asm
ptin    macro   ?num
ptin&?num   equ $
    ld  c,poll
    ld  e,plci&?num    ; E = 1, 3, 5, or 7 (device number)
    call    xdos       ; Poll single device
    in  a,(data&?num)  ; Read from THIS console's data port
    and 7fh            ; Strip parity
    ret
    endm
```

Key observations:
- Uses **simple poll** (DE < 256) for a single device
- D register is not explicitly set (contains previous value, but E < 16 suffices)
- After poll returns, reads from the **dedicated hardware port** for that console
- No overlap with other consoles possible

## SIMH Interrupt Handler

From `MPMXIOS.MAC` lines 522-567, the interrupt handler only sets system flags:

```asm
    ld  c,flagset
    ld  e,1
    call    xdos       ; Set flag #1 each tick
    ...
    ld  c,flagset
    ld  e,2
    call    xdos       ; Set flag #2 @ 1 sec
```

**No console polling occurs in the interrupt handler.** Console status is checked only when a process explicitly polls or calls CONST/CONIN.

## Why SIMH Works

1. **Hardware isolation**: Each console has dedicated I/O ports
2. **No input routing decisions**: Hardware determines which console receives input
3. **Simple poll only**: Each TMP polls only its own device (DE < 256)
4. **No poll conflicts**: Impossible for multiple consoles to see the same input

## Why Our Local Console Fails

| Aspect | SIMH | Our Emulator (Local Mode) |
|--------|------|---------------------------|
| Terminal per console | Yes (separate ports) | No (one shared terminal) |
| Input routing | Hardware determines | Software must choose |
| Poll type observed | Simple (DE < 256) | Poll table (DE >= 256) |
| Poll conflicts | Impossible | Possible/observed |

Our shared local terminal violates the hardware assumption that each console is physically separate. When we send input to console 3's queue:

1. TMP3 polls device 7 and sees input ready
2. But TMP3's poll table may include devices 7, 9, 11, 13, 15
3. XDOS poll table scan processes all entries
4. Final iteration may corrupt return state (observed bug)

In SIMH, this never happens because other consoles' status ports always show "no input" when typing on console 3's terminal.

## SSH Mode Replicates SIMH Model

Each SSH connection maps to a separate console, replicating the hardware model:

- SSH user connects → assigned to specific console (e.g., console 3)
- Input goes only to that console's queue
- TMP3 polls device 7 → finds input → calls CONIN
- Other TMPs poll their devices → find nothing → yield

This is functionally identical to SIMH's separate terminal connections.

## XDOS/Dispatcher Source Analysis

Analysis of the actual MP/M II source code (from `mpm2_external/mpm2src/NUCLEUS/`)
reveals that the dispatcher and XDOS are implemented correctly.

### XDOS POLL Function (XDOS.ASM lines 297-310)

```asm
@7:  ; function = 131, Poll Device
    DI                      ; Enter critical region
    MOV L,C
    MOV H,B
    SHLD DPARAM             ; Store parameter (device number)
    XCHG
    MVI M,3H                ; Set pd.status = poll_status
    RET                     ; Return to dispatcher
```

XDOS POLL simply:
1. Stores the device number in `dparam`
2. Sets the process status to `poll_status` (3)
3. Returns to the dispatcher (pushed earlier)

### Dispatcher Poll Handling (DSPTCH.ASM lines 976-1037)

```asm
@7:
    LXI D,PLR               ; Poll list root
    LHLD PLR
@36:                        ; Walk poll list
    MOV A,H
    ORA L
    JZ  @37                 ; End of list
    ...
    LXI H,10H
    DAD D
    MOV C,M                 ; C = pd.b (device number) - LOW BYTE ONLY!
    CALL XIOSPL             ; Poll this device
    ...
    RAR
    JNC @36                 ; Not ready, check next process
    ; Device ready - remove from poll list, add to run queue
    ...
```

Key insight: The dispatcher passes only the **low byte** of `pd.b` to XIOSPL.
This means each process polls ONE device, not a poll table.

### What "Multiple Devices Polled" Really Means

When we observed devices 7, 9, 11, 13, 15 being polled, this was:
- TMPs 3, 4, 5, 6, 7 all on the poll list
- Dispatcher checking each TMP's device in sequence
- NOT a "poll table" passed to XDOS

Each TMP calls XDOS POLL with its single device number:
- TMP3 polls device 7 (console 3 input)
- TMP4 polls device 9 (console 4 input)
- etc.

### TMP and CONIN Flow

From TMPSUB.ASM, TMP calls BDOS function 1 or 10 for console input.
BDOS (CONBDOS.ASM) calls XIOS CONIN (coninf).

Our BNKXIOS DO_CONIN matches SIMH exactly:
```asm
DO_CONIN:
    ; Calculate device = 2*console + 1
    LD  A, D
    ADD A, A
    INC A
    LD  D, 0                ; Simple poll (D < 256)
    LD  E, A                ; E = device
    LD  C, POLL
    CALL XDOS               ; Wait for input

    ; After POLL returns, read character
    LD  A, FUNC_CONIN
    OUT (XIOS_DISPATCH), A
    IN  A, (XIOS_DISPATCH)
    RET
```

### No XDOS Bug Found

The XDOS and dispatcher code is correct. When POLLDEVICE returns 0xFF:
1. Dispatcher removes process from poll list ✓
2. Dispatcher inserts process into run queue ✓
3. Process should resume after `CALL XDOS` in CONIN
4. CONIN should read character

But step 4 doesn't happen. Possible causes:
- Scheduling priority issues
- Stack/return address corruption
- Race condition between poll check and character read
- Process not actually resuming at expected location

**Modifying XDOS would NOT fix this issue** - the source code shows it's working correctly.

## Conclusion

The local console mode fails because it tries to multiplex one terminal across multiple consoles, violating MP/M II's assumption of physically separate console hardware. The SSH mode works correctly because it maintains the one-terminal-per-console model that SIMH and real MP/M II systems used.

The XDOS/dispatcher source analysis confirms there is no poll table bug - each TMP polls a single device, and the dispatcher correctly handles multiple waiting processes.

## References

- SIMH XIOS source: `simh_xios/MPMXIOS.MAC`
- MP/M II XDOS source: `mpm2_external/mpm2src/NUCLEUS/XDOS.ASM`
- MP/M II Dispatcher source: `mpm2_external/mpm2src/NUCLEUS/DSPTCH.ASM`
- MP/M II Console BDOS: `mpm2_external/mpm2src/NUCLEUS/CONBDOS.ASM`
- MP/M II TMP source: `mpm2_external/mpm2src/NUCLEUS/TMPSUB.ASM`
- Device numbers: Even (0,2,4...) = output, Odd (1,3,5...) = input
- Console N uses devices 2*N (output) and 2*N+1 (input)
