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
FUNC_COMMONBASE: EQU    4BH
FUNC_SWTUSER:   EQU     4EH
FUNC_SWTSYS:    EQU     51H
FUNC_PDISP:     EQU     54H
FUNC_XDOSENT:   EQU     57H
FUNC_SYSDAT:    EQU     5AH

; XDOS function codes
POLL:           EQU     131     ; XDOS poll function
FLAGWAIT:       EQU     132     ; XDOS flag wait
FLAGSET:        EQU     133     ; XDOS flag set

; Number of consoles
NMBCNS:         EQU     4       ; 4 consoles for SSH users

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
        JP      DO_IDLE         ; 48h - Idle procedure

; =============================================================================
; Commonbase structure - patched by GENSYS, used by XDOS/BNKBDOS
; These addresses are returned by BOOT and used for bank switching
; =============================================================================

COMMONBASE:
        JP      DO_BOOT         ; 4Bh - Cold start (returns HL = commonbase)
SWTUSER:
        JP      DO_SWTUSER      ; 4Eh - Switch to user bank
SWTSYS:
        JP      DO_SWTSYS       ; 51h - Switch to system bank
PDISP:
        JP      DO_PDISP        ; 54h - MP/M dispatcher
XDOS:
        JP      DO_XDOSENT      ; 57h - XDOS entry
SYSDAT:
        DW      0FF00H          ; 5Ah - System data page address (patched by GENSYS)

; =============================================================================
; Entry point implementations using port dispatch
; =============================================================================

DO_BOOT:
        ; Cold boot - returns HL = commonbase address
        LD      A, FUNC_BOOT
        OUT     (XIOS_DISPATCH), A
        RET

DO_WBOOT:
        ; Warm boot - terminate process via XDOS
        LD      C, 0
        JP      XDOS            ; System reset, terminate process

DO_CONST:
        ; Console status - D = console number
        ; Returns A = 0FFH if char ready, 00H if not
        LD      A, FUNC_CONST
        OUT     (XIOS_DISPATCH), A
        RET

DO_CONIN:
        ; Console input - D = console number
        ; Returns A = character
        ; Per MP/M II spec: WAIT until character is available
        ; Note: Using PDISP instead of POLL for better interrupt handling
CONIN_LOOP:
        ; First check if character is ready (CONST)
        LD      A, FUNC_CONST
        OUT     (XIOS_DISPATCH), A
        OR      A
        JR      NZ, CONIN_READY

        ; No character ready - yield to dispatcher
        ; This allows other processes to run without blocking
        PUSH    DE              ; Save console number
        CALL    PDISP           ; Yield to other processes
        POP     DE              ; Restore console number

        ; Check again
        JR      CONIN_LOOP

CONIN_READY:
        ; Character is ready - get it
        LD      A, FUNC_CONIN
        OUT     (XIOS_DISPATCH), A
        RET

DO_CONOUT:
        ; Console output - D = console number, C = character
        LD      A, FUNC_CONOUT
        OUT     (XIOS_DISPATCH), A
        RET

DO_LIST:
        ; List output - C = character
        LD      A, FUNC_LIST
        OUT     (XIOS_DISPATCH), A
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
        OUT     (XIOS_DISPATCH), A
        RET

DO_WRITE:
        ; Write sector - C = deblocking code
        ; Returns A = 0 success, A = 1 error
        LD      A, FUNC_WRITE
        OUT     (XIOS_DISPATCH), A
        RET

DO_LISTST:
        ; List status - returns A = 0FFH if ready
        LD      A, FUNC_LISTST
        OUT     (XIOS_DISPATCH), A
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
        OUT     (XIOS_DISPATCH), A
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
        LD      A, FUNC_EXITRGN
        OUT     (XIOS_DISPATCH), A
        LD      A, (PREEMP)
        OR      A
        RET     NZ
        EI
        RET

DO_MAXCON:
        ; Max console - returns A = number of consoles
        LD      A, NMBCNS
        RET

DO_SYSINIT:
        ; System initialization
        ; Set up interrupt handler at RST 7 (0038H)
        LD      A, 0C3H         ; JP opcode
        LD      (0038H), A
        LD      HL, INTHND
        LD      (0039H), HL

        ; Set interrupt mode 1 (Z80)
        IM      1
        EI

        ; Enable tick interrupts
        ; Note: XDOS should call STARTCLOCK, but in case it doesn't,
        ; enable it here to ensure preemption works
        LD      A, 0FFH
        LD      (TICKN), A
        LD      A, FUNC_STARTCLK
        OUT     (XIOS_DISPATCH), A

        ; Notify emulator
        LD      A, FUNC_SYSINIT
        OUT     (XIOS_DISPATCH), A
        RET

DO_IDLE:
        ; Idle procedure - called when no process is ready
        ; Enable interrupts and wait for the next tick
        EI
        HALT                    ; Wait for interrupt
        RET

DO_SWTUSER:
        ; Switch to user bank
        LD      A, FUNC_SWTUSER
        OUT     (XIOS_DISPATCH), A
        RET

DO_SWTSYS:
        ; Switch to system bank
        LD      A, FUNC_SWTSYS
        OUT     (XIOS_DISPATCH), A
        RET

DO_PDISP:
        ; Process dispatcher - called to switch processes
        ; Trap to emulator to re-enable interrupts and return
        LD      A, FUNC_PDISP
        OUT     (XIOS_DISPATCH), A
        RET

DO_XDOSENT:
        ; XDOS entry point
        ; Jump directly to XIOSJMP XDOS entry (FC00H + 57H = FC57H)
        ; This is patched by GENSYS to point to real XDOS
        JP      0FC57H

; =============================================================================
; Interrupt Handler - 60Hz tick
; Called via RST 38H (set up by SYSINIT)
; =============================================================================

INTHND:
        PUSH    AF
        PUSH    BC
        PUSH    DE
        PUSH    HL

        ; Set preempted flag
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
        ; Check console input devices and set flags if input ready
        ; Simplified: just poll devices without FLAGSET for now
        ; Device 1,3,5,7 -> Console 0,1,2,3
        LD      B, 4            ; Console count
        LD      D, 1            ; Start with device 1 (console 0 input)
CHKCON:
        LD      C, D            ; C = device number for POLLDEVICE
        LD      A, FUNC_POLLDEV
        OUT     (XIOS_DISPATCH), A
        ; Result in A - we're just polling to trigger wakeup check
        INC     D               ; Next device (skip by 2 to get odd numbers: 1,3,5,7)
        INC     D
        DJNZ    CHKCON

        ; Update 1-second counter (60 ticks = 1 second)
        LD      HL, CNT60
        DEC     (HL)
        JR      NZ, NOTSEC

        ; Reset counter and set flag #2 (1 second flag)
        ; Note: Load constant from memory because GENSYS incorrectly
        ; relocates immediate values that look like page numbers (0x3C = 60)
        LD      A, (CONST60)
        LD      (HL), A
        LD      C, FLAGSET
        LD      E, 2
        CALL    XDOS

NOTSEC:
        ; Clear preempted flag
        XOR     A
        LD      (PREEMP), A

        POP     HL
        POP     DE
        POP     BC
        POP     AF
        ; Note: Do NOT enable interrupts here
        ; The dispatcher enables them when resuming a process
        JP      PDISP           ; Dispatch to next ready process

; =============================================================================
; Data area
; =============================================================================

TICKN:  DB      0               ; Tick enable flag
CNT60:  DB      60              ; 60 Hz counter (for 1-second flag)
PREEMP: DB      0               ; Preempted flag
CONST60: DB     60              ; Constant 60 for resetting CNT60

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
        DW      0               ; CSV (not used)
        DW      ALV0            ; ALV address

; DPH for drive B
DPH1:
        DW      0
        DW      0, 0, 0
        DW      DIRBUF
        DW      DPB_8MB
        DW      0
        DW      ALV1

; DPH for drive C
DPH2:
        DW      0
        DW      0, 0, 0
        DW      DIRBUF
        DW      DPB_8MB
        DW      0
        DW      ALV2

; DPH for drive D
DPH3:
        DW      0
        DW      0, 0, 0
        DW      DIRBUF
        DW      DPB_8MB
        DW      0
        DW      ALV3

; Disk Parameter Block for hd1k format (8MB)
; 1024 dir entries * 32 bytes = 32KB = 8 blocks (0-7)
DPB_8MB:
        DW      64              ; SPT - 64 sectors per track (128-byte logical)
        DB      5               ; BSH - block shift (4K blocks)
        DB      31              ; BLM - block mask
        DB      1               ; EXM - extent mask
        DW      2039            ; DSM - disk size - 1 in blocks
        DW      1023            ; DRM - directory max - 1
        DB      0FFH            ; AL0 - blocks 0-7 for directory
        DB      000H            ; AL1 - blocks 8-15 are data
        DW      0               ; CKS - checksum vector size (0 = fixed disk)
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

; Buffers
DIRBUF: DS      128             ; Directory buffer (shared)
ALV0:   DS      256             ; Allocation vector drive A
ALV1:   DS      256             ; Allocation vector drive B
ALV2:   DS      256             ; Allocation vector drive C
ALV3:   DS      256             ; Allocation vector drive D

        END
