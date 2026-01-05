; sftp_glue.asm - Assembly glue for SFTP RSP
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Provides BDOS call and XIOS routines for PL/M code
; Symbol names must match uplm80 output (no $ separators)

        .Z80
        NAME    SFTP_GLUE
        CSEG

; BDOS entry point in bank 0 system memory
BDOS_ENTRY      EQU     0005H

;----------------------------------------------------------------------
; BDOS - Call BDOS with function and parameter
; Entry: uplm80 calling convention - args pushed on stack
;        Stack: [ret addr] [param low] [param high] [func]
; Exit:  A = result, HL = result (for address returns)
;----------------------------------------------------------------------
        PUBLIC  BDOS
BDOS:
        ; uplm80 pushes args: first the func (byte), then parm (address)
        ; So stack is: [ret] [parm lo] [parm hi] [func]
        pop     hl              ; save return address
        pop     de              ; get parm (address)
        pop     bc              ; get func in C (as byte, pushed as word)
        push    hl              ; restore return address
        call    BDOS_ENTRY      ; call BDOS
        ; Return value in A (HL for address results)
        ret

;----------------------------------------------------------------------
; SFTPPOLLWORK - Poll XIOS for pending SFTP work
; Entry: none
; Exit:  A = 0FFh if work pending, 00h if idle
;----------------------------------------------------------------------
        PUBLIC  SFTPPOLLWORK
SFTPPOLLWORK:
        ld      a, 60H          ; SFTP_POLL function code
        out     (0E0H), a       ; Dispatch to XIOS
        in      a, (0E0H)       ; Get result
        ret

;----------------------------------------------------------------------
; GETSFTPBUFADDR - Return address of SFTP buffer in bank 0
; The buffer is local to this module (not in common memory)
; Returns: BC = SFTPBUF address, HL = same
;----------------------------------------------------------------------
GETSFTPBUFADDR:
        ld      hl, SFTPBUF     ; Local buffer in bank 0
        ld      b, h            ; BC = SFTPBUF address
        ld      c, l
        ret

;----------------------------------------------------------------------
; GETBUFBYTE - Get byte from SFTPBUF at offset
; Entry: Stack has offset (word)
; Exit:  A = byte value
;----------------------------------------------------------------------
        PUBLIC  GETBUFBYTE
GETBUFBYTE:
        pop     hl              ; Return address
        pop     de              ; Offset (as word, E = low byte)
        push    hl              ; Restore return
        call    GETSFTPBUFADDR  ; HL = buffer base
        ld      d, 0            ; DE = offset
        add     hl, de          ; HL = buffer + offset
        ld      a, (hl)         ; A = byte value
        ret

;----------------------------------------------------------------------
; SETBUFBYTE - Set byte in SFTPBUF at offset
; Entry: Stack has offset (word), value (word, low byte used)
; Exit:  none
;----------------------------------------------------------------------
        PUBLIC  SETBUFBYTE
SETBUFBYTE:
        pop     hl              ; Return address
        pop     bc              ; Value (C = low byte)
        pop     de              ; Offset (E = low byte)
        push    hl              ; Restore return
        call    GETSFTPBUFADDR  ; HL = buffer base
        ld      d, 0            ; DE = offset
        add     hl, de          ; HL = buffer + offset
        ld      (hl), c         ; Store byte
        ret

;----------------------------------------------------------------------
; SFTPGETREQUEST - Get SFTP request from C++ into shared buffer
; Entry: none
; Exit:  A = 00h success, 0FFh no request
;----------------------------------------------------------------------
        PUBLIC  SFTPGETREQUEST
SFTPGETREQUEST:
        call    GETSFTPBUFADDR  ; BC = SFTPBUF address
        ld      a, 63H          ; SFTP_GET function code
        out     (0E0H), a       ; Dispatch to XIOS
        in      a, (0E0H)       ; Get result
        ret

;----------------------------------------------------------------------
; SFTPSENDREPLY - Send SFTP reply from shared buffer to C++
; Entry: none
; Exit:  none
;----------------------------------------------------------------------
        PUBLIC  SFTPSENDREPLY
SFTPSENDREPLY:
        call    GETSFTPBUFADDR  ; BC = SFTPBUF address
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
; SFTP Buffer - Local to bank 0 BRS module
; This is where SFTP requests/replies are exchanged with C++
; The C++ XIOS handler accesses this in bank 0 memory
; NOTE: Must use DB/DW to emit actual bytes (DS doesn't emit in PRL format)
;----------------------------------------------------------------------
        PUBLIC  SFTPBUF
SFTPBUF:
        ; 256-byte buffer for SFTP data (must use DW to emit bytes)
        REPT    128
        DW      0
        ENDM

; Note: INITSP_STACK has been moved to sftp_brs_header.asm

        END
