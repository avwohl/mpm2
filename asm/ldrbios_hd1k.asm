; ldrbios_hd1k.asm - MP/M II Loader BIOS for hd1k (8MB) disks
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Loader BIOS for the boot phase. Uses I/O port traps like xios_port.asm.
; This replaces PC-based interception with: LD B, function; OUT (0xE0), A; RET
;
; Assemble with: um80 -o ldrbios_hd1k.rel ldrbios_hd1k.asm

        .Z80                    ; Use Z80 mnemonics
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
; DPH for drive A (hd1k 8MB format)
DPH_A:
        DW      0               ; XLT - no sector translation
        DW      0,0,0           ; Scratch
        DW      DIRBUF          ; DIRBUF
        DW      DPB_HD1K        ; DPB for hd1k
        DW      0               ; CSV
        DW      ALV_A           ; ALV

; DPB for hd1k (8MB format - 1024 tracks, 64 logical sectors, 4K blocks)
; Matches RomWBW wbw_hd1k format
DPB_HD1K:
        DW      64              ; SPT - logical sectors per track (16 * 512 / 128)
        DB      5               ; BSH - block shift (4K blocks = 2^5 * 128)
        DB      31              ; BLM - block mask
        DB      1               ; EXM - extent mask
        DW      2039            ; DSM - disk size - 1 in blocks
        DW      1023            ; DRM - directory max - 1 (1024 entries)
        DB      0FFH            ; AL0 - 16 directory blocks
        DB      0FFH            ; AL1
        DW      0               ; CKS - no checksum (fixed disk)
        DW      2               ; OFF - reserved tracks

DIRBUF: DS      128             ; Directory buffer
ALV_A:  DS      256             ; Allocation vector (DSM/8 + 1 = 255 bytes)

        END
