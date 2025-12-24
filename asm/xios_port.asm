; xios_port.asm - MP/M II XIOS using I/O port dispatch
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; This XIOS uses I/O port traps instead of PC-based interception.
; Each entry point does: LD B, function; OUT (0xE0), A; RET
; The emulator reads B register to determine which function to call.
;
; Port Protocol:
;   0xE0 - XIOS dispatch (B = function offset, other regs as per XIOS spec)
;   0xE1 - Bank select (A = bank number)
;
; Assemble with: z80asm -o xios_port.bin xios_port.asm

        ORG     0FB00H          ; XIOS load address in common memory (XIOSJMP TBL)

; I/O Ports
XIOS_DISPATCH:  EQU     0E0H    ; XIOS dispatch (B = function)
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

; Number of consoles
NMBCNS:         EQU     8

; =============================================================================
; XIOS Jump Table - standard 3-byte entries
; =============================================================================

; Standard BIOS entries (00h-30h)
BOOT:       JP      DO_BOOT         ; 00h - Cold boot
WBOOT:      JP      DO_WBOOT        ; 03h - Warm boot
CONST:      JP      DO_CONST        ; 06h - Console status
CONIN:      JP      DO_CONIN        ; 09h - Console input
CONOUT:     JP      DO_CONOUT       ; 0Ch - Console output
LIST:       JP      DO_LIST         ; 0Fh - List output
PUNCH:      JP      DO_PUNCH        ; 12h - Punch output
READER:     JP      DO_READER       ; 15h - Reader input
HOME:       JP      DO_HOME         ; 18h - Home disk
SELDSK:     JP      DO_SELDSK       ; 1Bh - Select disk
SETTRK:     JP      DO_SETTRK       ; 1Eh - Set track
SETSEC:     JP      DO_SETSEC       ; 21h - Set sector
SETDMA:     JP      DO_SETDMA       ; 24h - Set DMA address
READ:       JP      DO_READ         ; 27h - Read sector
WRITE:      JP      DO_WRITE        ; 2Ah - Write sector
LISTST:     JP      DO_LISTST       ; 2Dh - List status
SECTRAN:    JP      DO_SECTRAN      ; 30h - Sector translate

; Extended MP/M II entries (33h-48h)
SELMEM:     JP      DO_SELMEM       ; 33h - Select memory bank
POLLDEV:    JP      DO_POLLDEV      ; 36h - Poll device
STARTCLK:   JP      DO_STARTCLK     ; 39h - Start clock
STOPCLK:    JP      DO_STOPCLK      ; 3Ch - Stop clock
EXITRGN:    JP      DO_EXITRGN      ; 3Fh - Exit region
MAXCON:     JP      DO_MAXCON       ; 42h - Max console
SYSINIT:    JP      DO_SYSINIT      ; 45h - System init
IDLE:       JP      DO_IDLE         ; 48h - Idle

; Commonbase entries (4Bh-5Ah)
COMMONBASE: JP      DO_COMMONBASE   ; 4Bh - Returns commonbase address
SWTUSER:    JP      DO_SWTUSER      ; 4Eh - Switch to user bank
SWTSYS:     JP      DO_SWTSYS       ; 51h - Switch to system bank
PDISP:      JP      DO_PDISP        ; 54h - Process dispatcher
XDOSENT:    JP      DO_XDOSENT      ; 57h - XDOS entry
SYSDAT:     DW      0FF00H          ; 5Ah - System data pointer

; =============================================================================
; Entry point implementations
; Each routine: LD B, function; OUT (dispatch_port), A; RET
; =============================================================================

DO_BOOT:
        LD      A, FUNC_BOOT
        OUT     (XIOS_DISPATCH), A
        RET

DO_WBOOT:
        LD      A, FUNC_WBOOT
        OUT     (XIOS_DISPATCH), A
        RET

DO_CONST:
        LD      A, FUNC_CONST
        OUT     (XIOS_DISPATCH), A
        RET

DO_CONIN:
        LD      A, FUNC_CONIN
        OUT     (XIOS_DISPATCH), A
        RET

DO_CONOUT:
        LD      A, FUNC_CONOUT
        OUT     (XIOS_DISPATCH), A
        RET

DO_LIST:
        LD      A, FUNC_LIST
        OUT     (XIOS_DISPATCH), A
        RET

DO_PUNCH:
        LD      A, FUNC_PUNCH
        OUT     (XIOS_DISPATCH), A
        RET

DO_READER:
        LD      A, FUNC_READER
        OUT     (XIOS_DISPATCH), A
        RET

DO_HOME:
        LD      A, FUNC_HOME
        OUT     (XIOS_DISPATCH), A
        RET

DO_SELDSK:
        LD      A, FUNC_SELDSK
        OUT     (XIOS_DISPATCH), A
        RET

DO_SETTRK:
        ; BC = track - save to HL for emulator
        LD      H, B
        LD      L, C
        LD      A, FUNC_SETTRK
        OUT     (XIOS_DISPATCH), A
        RET

DO_SETSEC:
        ; BC = sector - save to HL for emulator
        LD      H, B
        LD      L, C
        LD      A, FUNC_SETSEC
        OUT     (XIOS_DISPATCH), A
        RET

DO_SETDMA:
        ; BC = DMA address - save to HL for emulator
        LD      H, B
        LD      L, C
        LD      A, FUNC_SETDMA
        OUT     (XIOS_DISPATCH), A
        RET

DO_READ:
        LD      A, FUNC_READ
        OUT     (XIOS_DISPATCH), A
        RET

DO_WRITE:
        LD      A, FUNC_WRITE
        OUT     (XIOS_DISPATCH), A
        RET

DO_LISTST:
        LD      A, FUNC_LISTST
        OUT     (XIOS_DISPATCH), A
        RET

DO_SECTRAN:
        ; BC = logical sector - save to HL for emulator
        LD      H, B
        LD      L, C
        LD      A, FUNC_SECTRAN
        OUT     (XIOS_DISPATCH), A
        RET

DO_SELMEM:
        LD      A, FUNC_SELMEM
        OUT     (XIOS_DISPATCH), A
        RET

DO_POLLDEV:
        LD      A, FUNC_POLLDEV
        OUT     (XIOS_DISPATCH), A
        RET

DO_STARTCLK:
        LD      A, FUNC_STARTCLK
        OUT     (XIOS_DISPATCH), A
        RET

DO_STOPCLK:
        LD      A, FUNC_STOPCLK
        OUT     (XIOS_DISPATCH), A
        RET

DO_EXITRGN:
        LD      A, FUNC_EXITRGN
        OUT     (XIOS_DISPATCH), A
        RET

DO_MAXCON:
        LD      A, FUNC_MAXCON
        OUT     (XIOS_DISPATCH), A
        RET

DO_SYSINIT:
        LD      A, FUNC_SYSINIT
        OUT     (XIOS_DISPATCH), A
        RET

DO_IDLE:
        LD      A, FUNC_IDLE
        OUT     (XIOS_DISPATCH), A
        RET

DO_COMMONBASE:
        LD      A, FUNC_COMMONBASE
        OUT     (XIOS_DISPATCH), A
        RET

DO_SWTUSER:
        LD      A, FUNC_SWTUSER
        OUT     (XIOS_DISPATCH), A
        RET

DO_SWTSYS:
        LD      A, FUNC_SWTSYS
        OUT     (XIOS_DISPATCH), A
        RET

DO_PDISP:
        LD      A, FUNC_PDISP
        OUT     (XIOS_DISPATCH), A
        RET

DO_XDOSENT:
        LD      A, FUNC_XDOSENT
        OUT     (XIOS_DISPATCH), A
        RET

; =============================================================================
; Data area
; =============================================================================

TICKN:      DB      0           ; Tick enable flag
CNT60:      DB      60          ; 60 Hz counter
PREEMP:     DB      0           ; Preempted flag

        END
