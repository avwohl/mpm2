; ldrbios_port.asm - MP/M II Loader BIOS using I/O port dispatch
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Loader BIOS for the boot phase. Uses I/O port traps like xios_port.asm.
; This replaces PC-based interception with: LD B, function; OUT (0xE0), A; RET
;
; Assemble with: z80asm -o ldrbios_port.bin ldrbios_port.asm

        ORG     01700H          ; LDRBIOS location (matches MP/M II LDRBDOS expectation)

; I/O Ports (same as xios_port.asm)
XIOS_DISPATCH:  EQU     0E0H

; XIOS function offsets
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

; =============================================================================
; LDRBIOS Jump Table
; =============================================================================

        JP      BOOT            ; Cold start
        JP      WBOOT           ; Warm start
        JP      CONST           ; Console status
        JP      CONIN           ; Console input
        JP      CONOUT          ; Console output
        JP      LIST            ; List output
        JP      PUNCH           ; Punch output
        JP      READER          ; Reader input
        JP      HOME            ; Home disk
        JP      SELDSK          ; Select disk
        JP      SETTRK          ; Set track
        JP      SETSEC          ; Set sector
        JP      SETDMA          ; Set DMA address
        JP      READ            ; Read sector
        JP      WRITE           ; Write sector
        JP      LISTST          ; List status
        JP      SECTRAN         ; Sector translate

; =============================================================================
; Entry point implementations
; =============================================================================

BOOT:
        DI
        LD      SP, 0100H       ; Stack below TPA
        LD      A, FUNC_BOOT
        OUT     (XIOS_DISPATCH), A
        RET

WBOOT:
        LD      A, FUNC_WBOOT
        OUT     (XIOS_DISPATCH), A
        RET

CONST:
        LD      A, FUNC_CONST
        OUT     (XIOS_DISPATCH), A
        RET

CONIN:
        LD      A, FUNC_CONIN
        OUT     (XIOS_DISPATCH), A
        RET

CONOUT:
        ; C = character to output
        LD      A, FUNC_CONOUT
        OUT     (XIOS_DISPATCH), A
        RET

LIST:
        LD      A, FUNC_LIST
        OUT     (XIOS_DISPATCH), A
        RET

PUNCH:
        LD      A, FUNC_PUNCH
        OUT     (XIOS_DISPATCH), A
        RET

READER:
        LD      A, FUNC_READER
        OUT     (XIOS_DISPATCH), A
        RET

HOME:
        LD      BC, 0
        ; Fall through to SETTRK

SETTRK:
        ; BC = track number - save in HL for emulator
        LD      H, B
        LD      L, C            ; HL = track
        LD      A, FUNC_SETTRK
        OUT     (XIOS_DISPATCH), A
        RET

SELDSK:
        ; C = disk number
        ; LDRBIOS only supports drive A (C=0)
        ; Return HL = DPH address (must use own DPH, not emulator's)
        LD      A, C
        OR      A               ; Check if disk 0
        JR      NZ, SELDSK_ERR
        LD      HL, DPH_A       ; Return address of our DPH
        RET
SELDSK_ERR:
        LD      HL, 0           ; Return 0 for invalid disk
        RET

SETSEC:
        ; BC = sector number - save in HL for emulator
        LD      H, B
        LD      L, C            ; HL = sector
        LD      A, FUNC_SETSEC
        OUT     (XIOS_DISPATCH), A
        RET

SETDMA:
        ; BC = DMA address - save in HL for emulator
        LD      H, B
        LD      L, C            ; HL = DMA address
        LD      A, FUNC_SETDMA
        OUT     (XIOS_DISPATCH), A
        RET

READ:
        LD      A, FUNC_READ
        OUT     (XIOS_DISPATCH), A
        RET

WRITE:
        ; C = deblocking code
        LD      A, FUNC_WRITE
        OUT     (XIOS_DISPATCH), A
        RET

LISTST:
        LD      A, FUNC_LISTST
        OUT     (XIOS_DISPATCH), A
        RET

SECTRAN:
        ; BC = logical sector, DE = translation table
        ; LDRBIOS uses no sector translation (XLT=0 in DPH)
        ; Return HL = BC (physical = logical)
        LD      H, B
        LD      L, C
        RET

; =============================================================================
; Disk Parameter Headers (still needed for MPMLDR compatibility)
; =============================================================================

DPH_TABLE:
; DPH for drive A
DPH_A:
        DW      0               ; XLT
        DW      0,0,0           ; Scratch
        DW      DIRBUF          ; DIRBUF
        DW      DPB_SSSD        ; DPB
        DW      0               ; CSV
        DW      ALV_A           ; ALV

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

DIRBUF: DS      128             ; Directory buffer
ALV_A:  DS      32              ; Allocation vector (DSM/8 + 1 bytes)

        END
