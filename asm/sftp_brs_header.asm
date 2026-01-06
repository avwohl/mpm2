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
;
; IMPORTANT: GENSYS BRS relocation bug workaround
; GENSYS only relocates bytes that are 0x00. If an address has a non-zero
; high byte (e.g., SFTPMAIN at offset 0x04DB), the high byte won't be
; relocated. We work around this by computing the address at runtime:
;   1. Load ENTRY_POINT address (which IS relocated, since its high byte is 0x00)
;   2. Add constant offset (SFTPMAIN - ENTRY_POINT) to get actual SFTPMAIN address
;   3. JP (HL) to jump there

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
; Use runtime address calculation to work around GENSYS relocation bug
ENTRY_POINT:
        LD      SP, STACK_END           ; Set up proper stack
        ; DEBUG: Signal that entry point was reached
        LD      A, 6AH                  ; Debug function code
        OUT     (0E0H), A               ; Dispatch to XIOS
        ; Compute SFTPMAIN address at runtime:
        ; ENTRY_POINT is at offset 0x000C, gets relocated correctly
        ; SFTPMAIN is at offset 0x04EA, but JP target wouldn't be relocated by GENSYS
        ; Instead: load ENTRY_POINT (relocated), add constant offset
        ; NOTE: Using hardcoded offset because linker expression computes wrong value
        ;       After adding rename code: SFTPMAIN=0x06E8, ENTRY_POINT=0x000C
        ;       Offset = 0x06E8 - 0x000C = 0x06DC
        LD      HL, ENTRY_POINT         ; This address IS relocated (high byte = 0x00)
        ; DEBUG: Report HL value before adding offset
        LD      B, H
        LD      C, L
        LD      A, 6CH                  ; Debug: report ENTRY_POINT value
        OUT     (0E0H), A
        LD      DE, 06DCH               ; Hardcoded: SFTPMAIN(06E8) - ENTRY_POINT(000C)
        ADD     HL, DE                  ; HL = actual SFTPMAIN address
        ; DEBUG: Report final HL value via XIOS (C=low, B=high)
        LD      B, H
        LD      C, L
        LD      A, 6BH                  ; Debug: report computed address
        OUT     (0E0H), A
        JP      (HL)                    ; Jump via register - no relocation issue

; Entry address for RET - pointed to by INITSP
ENTRY_ADDR:
        DW      ENTRY_POINT     ; RET pops this into PC (relocated correctly)

; Local stack for RSP (128 bytes)
STACK_SPACE:
        REPT    64
        DW      0
        ENDM
STACK_END:

        END
