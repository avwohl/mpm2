; ldrbios.asm - MP/M II Loader BIOS for Emulator
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; The LDRBIOS is a minimal BIOS used only during the boot process.
; It is loaded along with MPMLDR at the start of the boot sequence.
; After MP/M loads, it's replaced by the full XIOS.
;
; Memory map during boot:
;   0000-00FF: Page zero (vectors, etc.)
;   0100-xxxx: MPMLDR code
;   yyyy-zzzz: LDRBIOS (this code)
;   FC00-FFFF: Will become XIOS after boot
;
; Assemble with: z80asm -o ldrbios.bin ldrbios.asm

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
        LD      SP,0100H        ; Stack below TPA

        ; Print boot message
        LD      HL,BOOTMSG
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
        LD      A,FUNC_CONST
        OUT     (DISPATCH_PORT),A
        RET

CONIN:
        ; Console input
        ; Returns A=character
        ; Dispatch to emulator via I/O port
        LD      A,FUNC_CONIN
        OUT     (DISPATCH_PORT),A
        RET

CONOUT:
        ; Console output
        ; C=character to output
        ; Dispatch to emulator via I/O port
        LD      A,FUNC_CONOUT
        OUT     (DISPATCH_PORT),A
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
        LD      A,1AH           ; Return EOF
        RET

HOME:
        ; Home disk to track 0
        LD      BC,0
        JP      SETTRK

SELDSK:
        ; Select disk
        ; C=drive number (0=A, etc.)
        ; Returns HL=DPH address, or 0 if error
        LD      A,C
        CP      4               ; Only A-D supported
        JR      NC,SELDSK_ERR

        ; Save current drive
        LD      (CURDSK),A

        ; Notify emulator (it tracks current disk for READ/WRITE)
        PUSH    BC
        LD      A,FUNC_SELDSK
        OUT     (DISPATCH_PORT),A
        POP     BC

        ; Calculate DPH address (use LDRBIOS local DPH)
        LD      H,0
        LD      L,C
        ADD     HL,HL           ; *2
        ADD     HL,HL           ; *4
        ADD     HL,HL           ; *8
        ADD     HL,HL           ; *16 (DPH size)
        LD      DE,DPH_TABLE
        ADD     HL,DE
        RET

SELDSK_ERR:
        LD      HL,0
        RET

SETTRK:
        ; Set track
        ; BC=track number
        ; Dispatch to emulator via I/O port (expects HL)
        LD      (CURTRK),BC
        LD      H,B
        LD      L,C
        LD      A,FUNC_SETTRK
        OUT     (DISPATCH_PORT),A
        RET

SETSEC:
        ; Set sector
        ; BC=sector number
        ; Dispatch to emulator via I/O port (expects HL)
        LD      (CURSEC),BC
        LD      H,B
        LD      L,C
        LD      A,FUNC_SETSEC
        OUT     (DISPATCH_PORT),A
        RET

SETDMA:
        ; Set DMA address
        ; BC=address
        ; Dispatch to emulator via I/O port (expects HL)
        LD      (CURDMA),BC
        LD      H,B
        LD      L,C
        LD      A,FUNC_SETDMA
        OUT     (DISPATCH_PORT),A
        RET

READ:
        ; Read sector
        ; Dispatch to emulator via I/O port
        ; Returns A=0 success, A=1 error
        LD      A,FUNC_READ
        OUT     (DISPATCH_PORT),A
        RET

WRITE:
        ; Write sector
        ; C=write type
        ; Dispatch to emulator via I/O port
        ; Returns A=0 success, A=1 error
        LD      A,FUNC_WRITE
        OUT     (DISPATCH_PORT),A
        RET

LISTST:
        ; List status
        ; Returns A=0 not ready, A=FF ready
        LD      A,0FFH          ; Always ready
        RET

SECTRAN:
        ; Sector translation
        ; BC=logical sector, DE=translation table
        ; Returns HL=physical sector
        ; For hd1k, no translation needed
        LD      H,B
        LD      L,C
        RET

; =============================================================================
; Helper routines
; =============================================================================

PRTSTR:
        ; Print null-terminated string
        ; HL=string address
        LD      A,(HL)
        OR      A
        RET     Z
        LD      C,A
        PUSH    HL
        CALL    CONOUT
        POP     HL
        INC     HL
        JR      PRTSTR

; =============================================================================
; Data area
; =============================================================================

BOOTMSG:
        DB      0DH,0AH
        DB      'MP/M II Loader BIOS',0DH,0AH
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
; DPH for drive A (8" SSSD for boot disk)
DPH_A:
        DW      0               ; XLT
        DW      0,0,0           ; Scratch
        DW      DIRBUF          ; DIRBUF
        DW      DPB_SSSD        ; DPB for ibm-3740
        DW      0               ; CSV
        DW      ALV_A           ; ALV

; DPH for drive B
DPH_B:
        DW      0
        DW      0,0,0
        DW      DIRBUF
        DW      DPB_HD1K
        DW      0
        DW      ALV_B

; DPH for drive C
DPH_C:
        DW      0
        DW      0,0,0
        DW      DIRBUF
        DW      DPB_HD1K
        DW      0
        DW      ALV_C

; DPH for drive D
DPH_D:
        DW      0
        DW      0,0,0
        DW      DIRBUF
        DW      DPB_HD1K
        DW      0
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
; Disk Parameter Block for hd1k
; =============================================================================

DPB_HD1K:
        DW      64              ; SPT - sectors per track
        DB      5               ; BSH - block shift (4K blocks)
        DB      31              ; BLM - block mask
        DB      1               ; EXM - extent mask
        DW      2039            ; DSM - disk size - 1
        DW      1023            ; DRM - directory max - 1
        DB      0FFH            ; AL0
        DB      0FFH            ; AL1
        DW      0               ; CKS - no checksum
        DW      2               ; OFF - reserved tracks

; =============================================================================
; Buffers
; =============================================================================

DIRBUF: DS      128             ; Directory buffer

; Allocation vectors (256 bytes each for hd1k)
ALV_A:  DS      256
ALV_B:  DS      256
ALV_C:  DS      256
ALV_D:  DS      256

; =============================================================================
; End of LDRBIOS
; =============================================================================

        END
