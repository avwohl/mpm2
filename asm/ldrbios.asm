; ldrbios.asm - MP/M II Loader BIOS for 8" SSSD (floppy) disks
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Loader BIOS for the boot phase. Uses I/O port traps.
; For 8" SSSD (ibm-3740 compatible) format - no skew.
;
; Assemble with: um80 -o ldrbios.rel ldrbios.asm

        .Z80                    ; Use Z80 mnemonics
        ORG     01700H          ; LDRBIOS location (matches MP/M II LDRBDOS expectation)

; =============================================================================
; LDRBIOS Jump Table
; Must match standard CP/M BIOS layout
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
; Constants
; =============================================================================

CONSOLE:        EQU     0       ; Boot console number
BOOTDRV:        EQU     0       ; Boot drive (A:)
DISPATCH_PORT:  EQU     0E0H    ; I/O port for emulator dispatch

; Function offsets for dispatch
FUNC_CONST:     EQU     006H
FUNC_CONIN:     EQU     009H
FUNC_CONOUT:    EQU     00CH
FUNC_SELDSK:    EQU     01BH
FUNC_SETTRK:    EQU     01EH
FUNC_SETSEC:    EQU     021H
FUNC_SETDMA:    EQU     024H
FUNC_READ:      EQU     027H
FUNC_WRITE:     EQU     02AH

; =============================================================================
; BIOS Entry Points
; The emulator intercepts these and handles in C++
; =============================================================================

BOOT:
        ; Cold boot - initialize and load system
        DI
        LD      SP, 0100H        ; Stack below TPA

        ; Print boot message
        LD      HL, BOOTMSG
        CALL    PRTSTR

        ; The loader will take over from here
        RET

WBOOT:
        ; Warm boot - not used by loader
        RET

CONST:
        ; Console status
        ; Returns A=0 if no char, A=FF if char ready
        ; Dispatch to emulator via I/O port
        LD      A, FUNC_CONST
        OUT     (DISPATCH_PORT), A
        RET

CONIN:
        ; Console input
        ; Returns A=character
        ; Dispatch to emulator via I/O port
        LD      A, FUNC_CONIN
        OUT     (DISPATCH_PORT), A
        RET

CONOUT:
        ; Console output
        ; C=character to output
        ; Dispatch to emulator via I/O port
        LD      A, FUNC_CONOUT
        OUT     (DISPATCH_PORT), A
        RET

LIST:
        ; List device output
        ; C=character
        RET

PUNCH:
        ; Punch output
        ; C=character
        RET

READER:
        ; Reader input
        ; Returns A=character (1AH=EOF)
        LD      A, 1AH           ; Return EOF
        RET

HOME:
        ; Home disk to track 0
        LD      BC, 0
        JP      SETTRK

SELDSK:
        ; Select disk
        ; C=drive number (0=A, etc.)
        ; Returns HL=DPH address, or 0 if error
        LD      A, C
        CP      4               ; Only A-D supported
        JR      NC, SELDSK_ERR

        ; Save current drive
        LD      (CURDSK), A

        ; Notify emulator (it tracks current disk for READ/WRITE)
        PUSH    BC
        LD      A, FUNC_SELDSK
        OUT     (DISPATCH_PORT), A
        POP     BC

        ; Calculate DPH address (use LDRBIOS local DPH)
        LD      H, 0
        LD      L, C
        ADD     HL, HL           ; *2
        ADD     HL, HL           ; *4
        ADD     HL, HL           ; *8
        ADD     HL, HL           ; *16 (DPH size)
        LD      DE, DPH_TABLE
        ADD     HL, DE
        RET

SELDSK_ERR:
        LD      HL, 0
        RET

SETTRK:
        ; Set track
        ; BC=track number
        ; Dispatch to emulator via I/O port (expects HL)
        LD      (CURTRK), BC
        LD      H, B
        LD      L, C
        LD      A, FUNC_SETTRK
        OUT     (DISPATCH_PORT), A
        RET

SETSEC:
        ; Set sector
        ; BC=sector number
        ; Dispatch to emulator via I/O port (expects HL)
        LD      (CURSEC), BC
        LD      H, B
        LD      L, C
        LD      A, FUNC_SETSEC
        OUT     (DISPATCH_PORT), A
        RET

SETDMA:
        ; Set DMA address
        ; BC=address
        ; Dispatch to emulator via I/O port (expects HL)
        LD      (CURDMA), BC
        LD      H, B
        LD      L, C
        LD      A, FUNC_SETDMA
        OUT     (DISPATCH_PORT), A
        RET

READ:
        ; Read sector
        ; Dispatch to emulator via I/O port
        ; Returns A=0 success, A=1 error
        LD      A, FUNC_READ
        OUT     (DISPATCH_PORT), A
        RET

WRITE:
        ; Write sector
        ; C=write type
        ; Dispatch to emulator via I/O port
        ; Returns A=0 success, A=1 error
        LD      A, FUNC_WRITE
        OUT     (DISPATCH_PORT), A
        RET

LISTST:
        ; List status
        ; Returns A=0 not ready, A=FF ready
        LD      A, 0FFH          ; Always ready
        RET

SECTRAN:
        ; Sector translation
        ; BC=logical sector, DE=translation table
        ; Returns HL=physical sector
        ; For hd1k, no translation needed
        LD      H, B
        LD      L, C
        RET

; =============================================================================
; Helper routines
; =============================================================================

PRTSTR:
        ; Print null-terminated string
        ; HL=string address
        LD      A, (HL)
        OR      A
        RET     Z
        LD      C, A
        PUSH    HL
        CALL    CONOUT
        POP     HL
        INC     HL
        JR      PRTSTR

; =============================================================================
; Data area
; =============================================================================

BOOTMSG:
        DB      0DH, 0AH
        DB      'MP/M II Loader BIOS', 0DH, 0AH
        DB      0

; Current disk parameters
CURDSK: DB      0               ; Current disk
CURTRK: DW      0               ; Current track
CURSEC: DW      0               ; Current sector
CURDMA: DW      0080H           ; Current DMA address

; =============================================================================
; Disk Parameter Headers for boot
; =============================================================================

DPH_TABLE:
; DPH for drive A (8" SSSD - no skew)
DPH_A:
        DW      0               ; XLT - no translation
        DW      0, 0, 0         ; Scratch
        DW      DIRBUF          ; DIRBUF
        DW      DPB_SSSD        ; DPB for 8" SSSD
        DW      CSV_A           ; CSV
        DW      ALV_A           ; ALV

; DPH for drive B
DPH_B:
        DW      0
        DW      0, 0, 0
        DW      DIRBUF
        DW      DPB_SSSD
        DW      CSV_B
        DW      ALV_B

; DPH for drive C
DPH_C:
        DW      0
        DW      0, 0, 0
        DW      DIRBUF
        DW      DPB_SSSD
        DW      CSV_C
        DW      ALV_C

; DPH for drive D
DPH_D:
        DW      0
        DW      0, 0, 0
        DW      DIRBUF
        DW      DPB_SSSD
        DW      CSV_D
        DW      ALV_D

; =============================================================================
; Disk Parameter Block for 8" SSSD (ibm-3740)
; =============================================================================

DPB_SSSD:
        DW      26              ; SPT - sectors per track
        DB      3               ; BSH - block shift (1K blocks)
        DB      7               ; BLM - block mask
        DB      0               ; EXM - extent mask
        DW      242             ; DSM - disk size - 1 (243 blocks)
        DW      63              ; DRM - directory max - 1 (64 entries)
        DB      0C0H            ; AL0 - first 2 blocks for directory
        DB      000H            ; AL1
        DW      16              ; CKS - checksum size
        DW      2               ; OFF - reserved tracks

; =============================================================================
; Buffers for 8" SSSD format
; DSM=242 -> ALV needs (242/8)+1 = 31 bytes
; DRM=63  -> CSV needs (63+1)/4 = 16 bytes
; =============================================================================

DIRBUF: DS      128             ; Directory buffer

; Allocation vectors (31 bytes each)
ALV_A:  DS      31
ALV_B:  DS      31
ALV_C:  DS      31
ALV_D:  DS      31

; Checksum vectors (16 bytes each)
CSV_A:  DS      16
CSV_B:  DS      16
CSV_C:  DS      16
CSV_D:  DS      16

; =============================================================================
; End of LDRBIOS
; =============================================================================

        END
