; sftp_glue.asm - Assembly glue for SFTP RSP
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Provides BDOS call and XIOS routines for PL/M code
; Symbol names must match uplm80 output (no $ separators)
;
; Each routine takes its arguments as sftp_brs.plm's EXTERNAL declarations
; pass them - PL/M-80's calling convention, which uplm80 uses: a single
; argument in BC (C for a BYTE), two in BC (C) then DE (E); a BYTE result in
; A, an ADDRESS one in HL; nothing else need be kept.

        .Z80
        NAME    SFTP_GLUE
        CSEG

        EXTRN   RSPBASE         ; RSP common module base address

;----------------------------------------------------------------------
; DELAY - XDOS delay
; Entry: BC = ticks to delay
; Exit: A = result
;
; Calls the BDOS directly, through the RSP's BDOS entry
;----------------------------------------------------------------------
        PUBLIC  DELAY
DELAY:
        ld      d, b            ; DE = ticks
        ld      e, c
        ld      c, 8DH          ; C = XDOS_DELAY function (141)
        ld      hl, (RSPBASE)   ; HL = RSP common module base
        ld      a, (hl)         ; low byte of bdos$entry
        inc     hl
        ld      h, (hl)         ; high byte of bdos$entry
        ld      l, a            ; HL = bdos$entry
        jp      (hl)            ; jump to BDOS (C=func, DE=parm)

;----------------------------------------------------------------------
; BDOS - Call BDOS with function and parameter
; Entry: C = function, DE = parameter
; Exit:  A = result, HL = result (for address returns)
;
; For banked RSPs, BDOS must be called through the bdos$entry address
; stored at offset 0 of the RSP common module (filled by GENSYS).
; We cannot call 0005H directly from bank 0.
;----------------------------------------------------------------------
        PUBLIC  BDOS
BDOS:
        ld      hl, (RSPBASE)   ; HL = RSP common module base
        ld      a, (hl)         ; get low byte of bdos$entry
        inc     hl
        ld      h, (hl)         ; get high byte of bdos$entry
        ld      l, a            ; HL = bdos$entry
        jp      (hl)            ; jump to bdos$entry (returns to our caller)

;----------------------------------------------------------------------
; The emulator leaves an XIOS function's result in A as part of the OUT.
; An IN A,(0E0H) after it only reads A back (src/mpm_cpu.cpp), so the two
; routines below that return a result no longer have one - as the XIOS
; routines in asm/bnkxios.asm no longer do.
;
;----------------------------------------------------------------------
; SFTPPOLLWORK - Poll XIOS for pending SFTP work
; Entry: none
; Exit:  A = 0FFh if work pending, 00h if idle
;----------------------------------------------------------------------
        PUBLIC  SFTPPOLLWORK
SFTPPOLLWORK:
        ld      a, 60H          ; SFTP_POLL function code
        out     (0E0H), a       ; Dispatch to XIOS - result comes back in A
        ret

;----------------------------------------------------------------------
; GETBUFBYTE - Get byte from SFTPBUF at offset
; Entry: BC = offset
; Exit:  A = byte value
;----------------------------------------------------------------------
        PUBLIC  GETBUFBYTE
GETBUFBYTE:
        ld      hl, SFTPBUF     ; HL = buffer base (gensys.py relocates correctly)
        add     hl, bc          ; HL = buffer + offset
        ld      a, (hl)         ; A = byte value
        ret

;----------------------------------------------------------------------
; SETBUFBYTE - Set byte in SFTPBUF at offset
; Entry: BC = offset, E = value
; Exit:  none
;----------------------------------------------------------------------
        PUBLIC  SETBUFBYTE
SETBUFBYTE:
        ld      hl, SFTPBUF     ; HL = buffer base (gensys.py relocates correctly)
        add     hl, bc          ; HL = buffer + offset
        ld      (hl), e         ; Store byte
        ret

;----------------------------------------------------------------------
; SFTPGETREQUEST - Get SFTP request from C++ into shared buffer
; Entry: none
; Exit:  A = 00h success, 0FFh no request
;----------------------------------------------------------------------
        PUBLIC  SFTPGETREQUEST
SFTPGETREQUEST:
        ld      bc, SFTPBUF     ; BC = SFTPBUF address (gensys.py relocates)
        ld      a, 63H          ; SFTP_GET function code
        out     (0E0H), a       ; Dispatch to XIOS - result comes back in A
        ret

;----------------------------------------------------------------------
; SFTPSENDREPLY - Send SFTP reply from shared buffer to C++
; Entry: none
; Exit:  none
;----------------------------------------------------------------------
        PUBLIC  SFTPSENDREPLY
SFTPSENDREPLY:
        ld      bc, SFTPBUF     ; BC = SFTPBUF address (gensys.py relocates)
        ld      a, 66H          ; SFTP_PUT function code
        out     (0E0H), a       ; Dispatch to XIOS
        ret

;----------------------------------------------------------------------
; SFTPHELLO - Signal RSP startup to C++ (debug)
; Entry: none
; Exit:  none
;----------------------------------------------------------------------
        PUBLIC  SFTPHELLO
SFTPHELLO:
        ld      a, 69H          ; SFTP_HELLO function code
        out     (0E0H), a       ; Dispatch to XIOS
        ret

;----------------------------------------------------------------------
; SFTPDEBUG - Debug trace output
; Entry: C = trace code
; Exit:  none
;----------------------------------------------------------------------
        PUBLIC  SFTPDEBUG
SFTPDEBUG:
        ld      a, 6DH          ; SFTP_DEBUG function code (the code is in C)
        out     (0E0H), a       ; Dispatch to XIOS
        ret

;----------------------------------------------------------------------
; SFTP Buffer - Local to bank 0 BRS module
; This is where SFTP requests/replies are exchanged with C++
; The C++ XIOS handler accesses this in bank 0 memory
; NOTE: Must use DW to emit actual bytes (DS doesn't emit in PRL format)
;----------------------------------------------------------------------
        PUBLIC  SFTPBUF
SFTPBUF:
        ; 2048-byte buffer for SFTP data
        REPT    1024
        DW      0
        ENDM

        END
