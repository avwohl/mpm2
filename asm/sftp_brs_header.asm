; sftp_brs_header.asm - BRS module header for SFTP RSP
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; This file defines the BRS header at the start of the module.
; It must be linked FIRST so these symbols are at the correct offsets.
;
; BRS header structure (per MP/M II System Guide):
; Offset 0-1: RSPBASE - pointer to RSP common module (filled by GENSYS)
; Offset 2-3: INITSP - initial stack pointer
; Offset 4-11: BRSNAME - 8-character name (must match RSP name)
; Offset 12+: Entry code
;
; When MP/M starts the BRS:
; 1. Switches to bank 0
; 2. Sets SP = INITSP (after relocation)
; 3. Does RET, which pops [SP] into PC
; So [INITSP] must contain the entry point address

        .Z80
        NAME    SFTP_BRS_HDR
        CSEG

        PUBLIC  RSPBASE
        PUBLIC  ENTRY_POINT
        EXTRN   SFTPMAIN        ; Main loop from PL/M code

; Header starts here at offset 0
RSPBASE:
        DW      0               ; Offset 0-1: filled by GENSYS

INITSP:
        DW      ENTRY_ADDR      ; Offset 2-3: SP points here before RET

BRSNAME:
        DB      'SFTP    '      ; Offset 4-11: 8-char name

; Offset 12: Entry code
ENTRY_POINT:
        LD      SP, STACK_END   ; Set up proper stack
        JP      SFTPMAIN        ; Jump to main loop (gensys.py relocates correctly)

; Entry address for RET - pointed to by INITSP
ENTRY_ADDR:
        DW      ENTRY_POINT     ; RET pops this into PC

; Local stack for RSP (128 bytes)
STACK_SPACE:
        REPT    64
        DW      0
        ENDM
STACK_END:

        END
