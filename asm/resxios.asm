; resxios.asm - Minimal RESXIOS for GENSYS
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; This is a minimal XIOS for use with GENSYS to create MPM.SYS.
; The actual XIOS functionality is in xios.asm and loaded via boot image.
; This file just provides the structure GENSYS expects.
;
; Assemble with: z80asm resxios.asm -o resxios.bin
; Convert to SPR with: mkspr resxios.bin RESXIOS.SPR

        ORG     0               ; Will be relocated by GENSYS

; =============================================================================
; XIOS Jump Table - Standard entries
; These are relative jumps to save relocation hassle
; =============================================================================

        JP      COMMONBASE      ; 00 - to commonbase
WBOOT:  JP      WARMSTART       ; 03 - warm boot
        JP      CONST_          ; 06 - console status
        JP      CONIN_          ; 09 - console input
        JP      CONOUT_         ; 0C - console output
        JP      LIST_           ; 0F - list output
        JP      RTNEMPTY        ; 12 - punch (not implemented)
        JP      RTNEMPTY        ; 15 - reader (not implemented)
        JP      HOME_           ; 18 - home disk
        JP      SELDSK_         ; 1B - select disk
        JP      SETTRK_         ; 1E - set track
        JP      SETSEC_         ; 21 - set sector
        JP      SETDMA_         ; 24 - set DMA
        JP      READ_           ; 27 - read sector
        JP      WRITE_          ; 2A - write sector
        JP      LISTST_         ; 2D - list status
        JP      SECTRAN_        ; 30 - sector translate

; MP/M II extended entries
        JP      SELMEM_         ; 33 - select memory
        JP      POLLDEV_        ; 36 - poll device
        JP      STRTCLK_        ; 39 - start clock
        JP      STOPCLK_        ; 3C - stop clock
        JP      EXITRG_         ; 3F - exit region
        JP      MAXCON_         ; 42 - max console
        JP      SYSINIT_        ; 45 - system init
        DB      0               ; 48 - use internal idle

; =============================================================================
; Common Base - patched by GENSYS
; =============================================================================

COMMONBASE:
        JP      COLDSTART
SWTUSER:JP      0               ; Switch to user bank
SWTSYS: JP      0               ; Switch to system bank
PDISP:  JP      0               ; MP/M dispatcher
XDOS:   JP      0               ; XDOS entry
SYSDAT: DW      0               ; System data page address

; =============================================================================
; Entry Point Implementations
; =============================================================================

COLDSTART:
WARMSTART:
        LD      C,0
        JP      XDOS            ; System reset

CONST_:
CONIN_:
        XOR     A               ; Return 0
        RET

CONOUT_:
LIST_:
RTNEMPTY:
HOME_:
SETTRK_:
SETSEC_:
SETDMA_:
        RET

SELDSK_:
        LD      HL,DPH0         ; Return DPH address
        RET

READ_:
WRITE_:
        XOR     A               ; Return success
        RET

LISTST_:
POLLDEV_:
        XOR     A               ; Not ready
        RET

SECTRAN_:
        LD      H,B
        LD      L,C             ; No translation
        RET

SELMEM_:
        RET

STRTCLK_:
        LD      A,0FFH
        LD      (TICKN),A
        RET

STOPCLK_:
        XOR     A
        LD      (TICKN),A
        RET

EXITRG_:
        LD      A,(PREEMP)
        OR      A
        RET     NZ
        EI
        RET

MAXCON_:
        LD      A,1             ; 1 console
        RET

SYSINIT_:
        ; Set up RST 38H handler
        LD      A,0C3H          ; JP opcode
        LD      (0038H),A
        LD      HL,INTHND
        LD      (0039H),HL
        IM      1
        EI
        RET

; =============================================================================
; Interrupt Handler
; =============================================================================

INTHND:
        PUSH    AF
        PUSH    BC
        PUSH    DE
        PUSH    HL

        LD      A,0FFH
        LD      (PREEMP),A      ; Set preempted flag

        ; Clock tick
        LD      A,(TICKN)
        OR      A
        JR      Z,NOTICK
        LD      C,133           ; flagset
        LD      E,1             ; flag 1
        CALL    XDOS

NOTICK:
        ; 1-second counter
        LD      HL,CNT60
        DEC     (HL)
        JR      NZ,NOTSEC
        LD      (HL),60
        LD      C,133           ; flagset
        LD      E,2             ; flag 2
        CALL    XDOS

NOTSEC:
        XOR     A
        LD      (PREEMP),A      ; Clear preempted flag

        POP     HL
        POP     DE
        POP     BC
        POP     AF
        EI
        JP      PDISP           ; Dispatch

; =============================================================================
; Data
; =============================================================================

TICKN:  DB      0
CNT60:  DB      60
PREEMP: DB      0

; Minimal DPH
DPH0:   DW      0               ; XLT
        DW      0,0,0           ; Scratch
        DW      DIRBUF          ; Directory buffer
        DW      DPB             ; DPB pointer
        DW      0               ; CSV
        DW      ALV             ; ALV pointer

DPB:    DW      64              ; SPT
        DB      5               ; BSH
        DB      31              ; BLM
        DB      1               ; EXM
        DW      2039            ; DSM
        DW      1023            ; DRM
        DB      0FFH            ; AL0
        DB      0FFH            ; AL1
        DW      0               ; CKS
        DW      2               ; OFF

DIRBUF: DS      128
ALV:    DS      256

        END
