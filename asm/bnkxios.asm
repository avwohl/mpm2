	;; printed at start increment every change to be sure we
;; get the right version
BNK_VERSION	EQU 24
; bnkxios_port.asm - MP/M II BNKXIOS using I/O port dispatch
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Complete Banked XIOS for MP/M II using I/O port traps.
; Each entry point does: LD A, function; OUT (0xE0), A; RET
; The emulator reads A register to determine which function to call.
;
; This is designed to be loaded as BNKXIOS.SPR or patched into memory.
;
; Assemble with: um80 -o bnkxios_port.rel bnkxios_port.asm
;                ul80 --prl -o BNKXIOS_port.SPR bnkxios_port.rel

        .Z80                    ; Use Z80 mnemonics

        ; PRL/SPR format: code assembled at 0x0100 (page 1), relocatable
        ; GENSYS handles relocation correctly - addresses adjusted by base page

; I/O Ports
XIOS_DISPATCH:  EQU     0E0H    ; XIOS dispatch (A = function)
BANK_SELECT:    EQU     0E1H    ; Bank select (A = bank)

; XIOS function offsets (must match xios.h constants)
FUNC_BOOT:      EQU     00H
FUNC_WBOOT:     EQU     03H
FUNC_CONST:     EQU     06H
FUNC_CONIN:     EQU     09H
FUNC_CONOUT:    EQU     0CH
FUNC_LIST:      EQU     0FH
FUNC_PUNCH:     EQU     12H
FUNC_READER:    EQU     15H
FUNC_HOME:      EQU     18H
FUNC_SELDSK:    EQU     1BH
FUNC_SETTRK:    EQU     1EH
FUNC_SETSEC:    EQU     21H
FUNC_SETDMA:    EQU     24H
FUNC_READ:      EQU     27H
FUNC_WRITE:     EQU     2AH
FUNC_LISTST:    EQU     2DH
FUNC_SECTRAN:   EQU     30H
FUNC_SELMEM:    EQU     33H
FUNC_POLLDEV:   EQU     36H
FUNC_STARTCLK:  EQU     39H
FUNC_STOPCLK:   EQU     3CH
FUNC_EXITRGN:   EQU     3FH
FUNC_MAXCON:    EQU     42H
FUNC_SYSINIT:   EQU     45H
FUNC_IDLE:      EQU     48H

; XDOS function codes
POLL:           EQU     131     ; XDOS poll function
FLAGWAIT:       EQU     132     ; XDOS flag wait
FLAGSET:        EQU     133     ; XDOS flag set

; Number of consoles
NMBCNS:         EQU     4       ; consoles for SSH users

; =============================================================================
; BNKXIOS Jump Vector
; Entry at offset 0 is COMMONBASE, not COLDBOOT (MP/M II convention)
; =============================================================================

        JP      COMMONBASE      ; 00h - Commonbase (not coldboot)
WBOOT:  JP      DO_WBOOT        ; 03h - Warm start
        JP      DO_CONST        ; 06h - Console status
        JP      DO_CONIN        ; 09h - Console input
        JP      DO_CONOUT       ; 0Ch - Console output
        JP      DO_LIST         ; 0Fh - List output
        JP      DO_PUNCH        ; 12h - Punch output
        JP      DO_READER       ; 15h - Reader input
        JP      DO_HOME         ; 18h - Home disk
        JP      DO_SELDSK       ; 1Bh - Select disk
        JP      DO_SETTRK       ; 1Eh - Set track
        JP      DO_SETSEC       ; 21h - Set sector
        JP      DO_SETDMA       ; 24h - Set DMA address
        JP      DO_READ         ; 27h - Read sector
        JP      DO_WRITE        ; 2Ah - Write sector
        JP      DO_LISTST       ; 2Dh - List status
        JP      DO_SECTRAN      ; 30h - Sector translate
        JP      DO_SELMEM       ; 33h - Select memory bank
        JP      DO_POLLDEV      ; 36h - Poll device
        JP      DO_STARTCLK     ; 39h - Start clock
        JP      DO_STOPCLK      ; 3Ch - Stop clock
        JP      DO_EXITRGN      ; 3Fh - Exit region
        JP      DO_MAXCON       ; 42h - Max console
        JP      DO_SYSINIT      ; 45h - System init
        DB      0,0,0           ; 48h - Idle (patched by GENSYS)

; =============================================================================
; Commonbase structure - patched by GENSYS, used by XDOS/BNKBDOS
; GENSYS patches SWTUSER, SWTSYS, PDISP, XDOS, SYSDAT to real addresses
; Use JP $-$ as placeholders (generates JP 0000 which GENSYS recognizes)
; =============================================================================

COMMONBASE:
        JP      DO_BOOT         ; 4Bh - Cold start (returns HL = commonbase)
SWTUSER:
        JP      $-$             ; 4Eh - Switch to user bank (patched by GENSYS)
SWTSYS:
        JP      $-$             ; 51h - Switch to system bank (patched by GENSYS)
PDISP:
        JP      $-$             ; 54h - MP/M dispatcher (patched by GENSYS -> XDOS+3)
XDOS:
        JP      $-$             ; 57h - XDOS entry (patched by GENSYS -> XDOS+6)
SYSDAT:
        DW      $-$             ; 5Ah - System data page address (patched by GENSYS)

; =============================================================================
; Entry point implementations using port dispatch
; =============================================================================

DO_BOOT:
DO_WBOOT:
        ; Cold boot - returns HL = commonbase address
        LD      A, FUNC_BOOT
        OUT     (XIOS_DISPATCH), A

        ; Warm boot - terminate process via XDOS
        LD      C, 0
        JP      XDOS            ; System reset, terminate process

DO_CONST:
        ; Console status - D = console number
        ; Returns A = 0FFH if char ready, 00H if not
        LD      A, FUNC_CONST
        OUT     (XIOS_DISPATCH), A      ; Dispatch function
        IN      A, (XIOS_DISPATCH)      ; Get result in A
        RET

DO_CONIN:
        ; Console input - D = console number
        ; Returns A = character
        ; Poll for input ready before reading
        LD      H, D            ; H = console (save for later)
        PUSH    HL              ; Save on stack

        LD      A, D            ; A = console
        ADD     A, A            ; A = 2*console
        INC     A               ; A = 2*console + 1 (input device)
        LD      E, A            ; E = device number
        LD      D, 0            ; D = 0 for simple poll
        LD      C, POLL
        CALL    XDOS            ; Wait for input ready

        POP     HL              ; Restore saved value
        LD      D, H            ; D = console

        LD      A, FUNC_CONIN
        OUT     (XIOS_DISPATCH), A
        IN      A, (XIOS_DISPATCH)
        RET

DO_CONOUT:
        ; Console output - D = console number, C = character
        ; Poll for output ready before sending (flow control)
        ; Save console in H, character in L
        LD      H, D            ; H = console
        LD      L, C            ; L = character
        PUSH    HL              ; Save both

        LD      A, D            ; A = console
        ADD     A, A            ; A = 2*console (output device)
        LD      E, A            ; E = device number
        LD      D, 0            ; D = 0 for simple poll
        LD      C, POLL
        CALL    XDOS            ; Wait for output ready

        POP     HL              ; Restore saved values
        LD      D, H            ; D = console
        LD      C, L            ; C = character

        LD      A, FUNC_CONOUT
        OUT     (XIOS_DISPATCH), A
        RET

DO_LIST:
        RET

DO_PUNCH:
        ; Punch output - not implemented
        XOR     A
        RET

DO_READER:
        ; Reader input - not implemented
        LD      A, 1AH          ; Return EOF (Ctrl-Z)
        RET

DO_HOME:
        ; Home disk - move to track 0
        LD      A, FUNC_HOME
        OUT     (XIOS_DISPATCH), A
        RET

DO_SELDSK:
        ; Select disk - C = disk number
        ; Returns HL = DPH address, or 0 if error
        ; First notify emulator of disk selection
        LD      A, FUNC_SELDSK
        OUT     (XIOS_DISPATCH), A
        ; Check if emulator returned error (A = 0xFF)
        CP      0FFH
        JR      Z, SELDSK_ERR
        ; Calculate DPH address = DPH_TABLE + (disk * 16)
        LD      A, C            ; A = disk number
        RLCA                    ; *2
        RLCA                    ; *4
        RLCA                    ; *8
        RLCA                    ; *16
        LD      L, A
        LD      H, 0
        LD      DE, DPH_TABLE
        ADD     HL, DE          ; HL = DPH_TABLE + disk*16
        RET
SELDSK_ERR:
        LD      HL, 0           ; Return 0 = error
        RET

DO_SETTRK:
        ; Set track - BC = track number
        ; Copy to HL for emulator
        LD      H, B
        LD      L, C
        LD      A, FUNC_SETTRK
        OUT     (XIOS_DISPATCH), A
        RET

DO_SETSEC:
        ; Set sector - BC = sector number
        ; Copy to HL for emulator
        LD      H, B
        LD      L, C
        LD      A, FUNC_SETSEC
        OUT     (XIOS_DISPATCH), A
        RET

DO_SETDMA:
        ; Set DMA address - BC = DMA address
        ; Copy to HL for emulator
        LD      H, B
        LD      L, C
        LD      A, FUNC_SETDMA
        OUT     (XIOS_DISPATCH), A
        RET

DO_READ:
        ; Read sector - returns A = 0 success, A = 1 error
        LD      A, FUNC_READ
        OUT     (XIOS_DISPATCH), A      ; Dispatch function
        IN      A, (XIOS_DISPATCH)      ; Get result in A
        RET

DO_WRITE:
        ; Write sector - C = deblocking code
        ; Returns A = 0 success, A = 1 error
        LD      A, FUNC_WRITE
        OUT     (XIOS_DISPATCH), A      ; Dispatch function
        IN      A, (XIOS_DISPATCH)      ; Get result in A
        RET

DO_LISTST:
        ; List status - returns A = 0FFH if ready
        LD      A, FUNC_LISTST
        OUT     (XIOS_DISPATCH), A      ; Dispatch function
        IN      A, (XIOS_DISPATCH)      ; Get result in A
        RET

DO_SECTRAN:
        ; Sector translate - BC = logical sector, DE = translation table
        ; Returns HL = physical sector
        ; Copy BC to HL for emulator
        LD      H, B
        LD      L, C
        LD      A, FUNC_SECTRAN
        OUT     (XIOS_DISPATCH), A
        RET

DO_SELMEM:
        ; Select memory bank - BC = memory descriptor address
        ; Descriptor format: base(1), size(1), attrib(1), bank(1)
        ; Dispatch to emulator which reads bank from descriptor+3
        LD      A, FUNC_SELMEM
        OUT     (XIOS_DISPATCH), A
        RET

DO_POLLDEV:
        ; Poll device - C = device number
        ; Returns A = 0FFH if ready, 00H if not
        LD      A, FUNC_POLLDEV
        OUT     (XIOS_DISPATCH), A      ; Dispatch function
        IN      A, (XIOS_DISPATCH)      ; Get result in A
        RET

DO_STARTCLK:
        ; Start clock - enable tick interrupts
        LD      A, 0FFH
        LD      (TICKN), A
        LD      A, FUNC_STARTCLK
        OUT     (XIOS_DISPATCH), A
        RET

DO_STOPCLK:
        ; Stop clock - disable tick interrupts
        XOR     A
        LD      (TICKN), A
        LD      A, FUNC_STOPCLK
        OUT     (XIOS_DISPATCH), A
        RET

DO_EXITRGN:
        ; Exit region - enable interrupts if not preempted
        ; Trace via port dispatch
        LD      A, (PREEMP)
        OR      A
        RET     NZ
        EI
        RET

DO_MAXCON:
        ; Max console - returns A = number of consoles
        ; Sample XIOS code returns nmbcns directly, not nmbcns-1
        ; For 8 consoles, return 8
        ; DEBUG: Use FUNC_MAXCON to let emulator trace the call
        LD      A, FUNC_MAXCON
        OUT     (XIOS_DISPATCH), A
        IN      A, (XIOS_DISPATCH)      ; Get result from emulator
        RET

DO_SYSINIT:
        ; System initialization
        ; Input parameters (from MP/M):
        ;   C  = MP/M debugger restart # (0-7 for RST 0-7)
        ;   DE = MP/M debugger entry point address
        ;   HL = BIOS direct jump table address
        ;
        ; We set up low memory in bank 0 here. The emulator's FUNC_SYSINIT
        ; handler will copy the low 256 bytes to all other banks.

        ; Calculate debugger RST address: addr = C * 8
        LD      A, C
        AND     07H                     ; Ensure valid RST number (0-7)
        RLCA                            ; *2
        RLCA                            ; *4
        RLCA                            ; *8
        LD      C, A                    ; C = RST address (0, 8, 16, ..., 56)

        ; Set JMP at 0x0000 -> BIOS direct jump table (HL)
        LD      A, 0C3H                 ; JP opcode
        LD      (0000H), A
        LD      (0001H), HL             ; Store address from HL

        ; Set JMP at debugger RST address -> debugger entry point (DE)
        ; C = RST address, use it as low byte of pointer
        LD      L, C
        LD      H, 0                    ; HL = RST address
        LD      A, 0C3H                 ; JP opcode
        LD      (HL), A
        INC     HL
        LD      (HL), E                 ; Low byte of debugger entry (from DE)
        INC     HL
        LD      (HL), D                 ; High byte of debugger entry

        ; Set JMP at 0x0005 -> XDOS entry (COMMONBASE+12 has JP XDOS)
        ; This is the CP/M-compatible BDOS entry point
        LD      A, 0C3H                 ; JP opcode
        LD      (0005H), A
        LD      HL, COMMONBASE+12       ; XDOS entry in jump table
        LD      (0006H), HL

        ; Set JMP at 0x0038 (RST 7) -> interrupt handler (INTHND)
        LD      A, 0C3H                 ; JP opcode
        LD      (0038H), A
        LD      HL, INTHND
        LD      (0039H), HL

        ; Set interrupt mode 1 (Z80)
        IM      1

        ; Enable TICKN immediately - XDOS's STARTCLOCK call isn't reliable in emulator
        ; (In real hardware, STARTCLOCK is called by dispatcher when delay list is empty)
        LD      A, 0FFH
        LD      (TICKN), A

        ; Debug: pass COMMONBASE and version to emulator
        LD      DE, COMMONBASE
        LD      HL, BNK_VERSION

        ; Notify emulator of initialization
        ; The emulator will copy low 256 bytes of bank 0 to all other banks
        LD      A, FUNC_SYSINIT
        OUT     (XIOS_DISPATCH), A

        ; IMPORTANT: Enable interrupts LAST, after all initialization
        EI
        RET


; =============================================================================
; Interrupt Handler - 60Hz tick
; Called via RST 38H (set up by SYSINIT)
; =============================================================================

INTHND:
        ; CRITICAL: Switch to dedicated interrupt stack BEFORE pushing anything
        ; This prevents corruption of SYSDAT if interrupted code had SP near 0xFFFF
        LD      (SAVED_SP), SP          ; Save current SP (uses only HL')
        LD      SP, INT_STACK           ; Use dedicated interrupt stack

        PUSH    AF
        PUSH    BC
        PUSH    DE
        PUSH    HL

        ; Set preempted flag in memory
        LD      A, 0FFH
        LD      (PREEMP), A

        ; Check if clock tick is enabled
        LD      A, (TICKN)
        OR      A
        JR      Z, NOTICK

        ; Set flag #1 (tick flag) via XDOS
        LD      C, FLAGSET
        LD      E, 1
        CALL    XDOS

NOTICK:
        ; SIMH style: Don't poll consoles here - just set tick/second flags
        ; Console polling from interrupt was causing XDOS reentrancy issues
        ; The POLL mechanism should work via XDOS dispatcher rescheduling

        ; Update 1-second counter (60 ticks = 1 second)
        LD      HL, CNT60
        DEC     (HL)
        JR      NZ, NOTSEC

        ; Reset counter and set flag #2 (1 second flag)
        LD      A, 60
        LD      (HL), A
        LD      C, FLAGSET
        LD      E, 2
        CALL    XDOS

NOTSEC:
        ; Clear preempted flag in memory
        XOR     A
        LD      (PREEMP), A

        POP     HL
        POP     DE
        POP     BC
        POP     AF

        ; Restore original SP before dispatching
        LD      SP, (SAVED_SP)

        ; Jump to dispatcher (PDISP is patched by GENSYS to XDOS+3)
        ; XDOS dispatcher will enable interrupts when appropriate
        JP      PDISP

; =============================================================================
; Data area
; =============================================================================

TICKN:  DB      0               ; Tick enable flag
CNT60:  DB      60              ; 60 Hz counter (for 1-second flag)
PREEMP: DB      0               ; Preempted flag

; Interrupt stack (prevents corruption of SYSDAT if interrupted SP is near 0xFFFF)
SAVED_SP: DW    0               ; Saved SP from interrupted code
        ; 32 bytes of stack space for interrupt handler
        ; (PUSH AF,BC,DE,HL = 8 bytes, plus XDOS call overhead)
        DB      0,0,0,0,0,0,0,0
        DB      0,0,0,0,0,0,0,0
        DB      0,0,0,0,0,0,0,0
        DB      0,0,0,0,0,0,0,0
INT_STACK:      ; Stack grows down, so label is at top

; =============================================================================
; Disk Parameter Headers and DPBs
; The emulator provides actual disk parameters via SELDSK return value
; These are minimal placeholders for compatibility
; =============================================================================

DPH_TABLE:
; DPH for drive A
DPH0:
        DW      0               ; XLT (no translation)
        DW      0, 0, 0         ; Scratch area
        DW      DIRBUF          ; DIRBUF address
        DW      DPB_8MB         ; DPB address
        DW      CSV0            ; CSV (provide buffer even with CKS=0)
        DW      ALV0            ; ALV address

; DPH for drive B
DPH1:
        DW      0
        DW      0, 0, 0
        DW      DIRBUF
        DW      DPB_8MB
        DW      CSV1
        DW      ALV1

; DPH for drive C
DPH2:
        DW      0
        DW      0, 0, 0
        DW      DIRBUF
        DW      DPB_8MB
        DW      CSV2
        DW      ALV2

; DPH for drive D
DPH3:
        DW      0
        DW      0, 0, 0
        DW      DIRBUF
        DW      DPB_8MB
        DW      CSV3
        DW      ALV3

; Disk Parameter Block for hd1k format (8MB)
; 1024 dir entries * 32 bytes = 32KB = 8 blocks (0-7)
; CKS = 0 for fixed disk (no directory checksums)
DPB_8MB:
        DW      64              ; SPT - 64 sectors per track (128-byte logical)
        DB      5               ; BSH - block shift (4K blocks)
        DB      31              ; BLM - block mask
        DB      1               ; EXM - extent mask
        DW      2039            ; DSM - disk size - 1 in blocks
        DW      1023            ; DRM - directory max - 1
        DB      0FFH            ; AL0 - blocks 0-7 for directory
        DB      000H            ; AL1 - blocks 8-15 are data
        DW      0               ; CKS - no checksums (fixed disk)
        DW      2               ; OFF - reserved tracks

; DPB for 8" SSSD (IBM 3740 standard - 77 tracks, 26 sectors, 128 bytes)
DPB_SSSD:
        DW      26              ; SPT - sectors per track
        DB      3               ; BSH - block shift (1K blocks)
        DB      7               ; BLM - block mask
        DB      0               ; EXM - extent mask
        DW      242             ; DSM - disk size - 1 in blocks
        DW      63              ; DRM - directory max - 1
        DB      0C0H            ; AL0 - 2 directory blocks
        DB      000H            ; AL1
        DW      16              ; CKS - checksum vector size
        DW      2               ; OFF - reserved tracks

; Buffers - use explicit DB 0 instead of DS so bytes are included in SPR output
; (ul80 --prl doesn't include DS sections, causing truncated SPR files)

; DIRBUF: 128 bytes
DIRBUF:
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 16
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 32
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 48
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 64
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 80
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 96
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 112
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 128

; ALV0: 256 bytes - Allocation vector drive A
ALV0:
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 16
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 32
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 48
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 64
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 80
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 96
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 112
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 128
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 144
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 160
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 176
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 192
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 208
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 224
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 240
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 256

; ALV1: 256 bytes - Allocation vector drive B
ALV1:
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 16
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 32
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 48
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 64
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 80
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 96
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 112
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 128
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 144
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 160
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 176
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 192
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 208
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 224
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 240
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 256

; ALV2: 256 bytes - Allocation vector drive C
ALV2:
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 16
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 32
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 48
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 64
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 80
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 96
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 112
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 128
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 144
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 160
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 176
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 192
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 208
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 224
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 240
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 256

; ALV3: 256 bytes - Allocation vector drive D
ALV3:
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 16
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 32
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 48
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 64
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 80
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 96
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 112
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 128
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 144
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 160
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 176
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 192
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 208
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 224
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 240
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 256

; CSV0: 256 bytes - Checksum vector drive A
CSV0:
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 16
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 32
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 48
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 64
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 80
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 96
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 112
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 128
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 144
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 160
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 176
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 192
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 208
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 224
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 240
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 256

; CSV1: 256 bytes - Checksum vector drive B
CSV1:
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 16
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 32
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 48
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 64
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 80
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 96
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 112
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 128
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 144
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 160
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 176
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 192
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 208
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 224
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 240
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 256

; CSV2: 256 bytes - Checksum vector drive C
CSV2:
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 16
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 32
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 48
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 64
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 80
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 96
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 112
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 128
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 144
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 160
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 176
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 192
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 208
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 224
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 240
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 256

; CSV3: 256 bytes - Checksum vector drive D
CSV3:
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 16
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 32
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 48
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 64
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 80
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 96
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 112
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 128
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 144
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 160
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 176
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 192
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 208
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 224
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 240
        DB      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0  ; 256

        END
