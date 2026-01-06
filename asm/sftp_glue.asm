; sftp_glue.asm - Assembly glue for SFTP RSP
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; Provides BDOS call and XIOS routines for PL/M code
; Symbol names must match uplm80 output (no $ separators)

        .Z80
        NAME    SFTP_GLUE
        CSEG

        EXTRN   RSPBASE         ; RSP common module base address

;----------------------------------------------------------------------
; DELAY - Custom delay implementation that bypasses ??AUTO issue
; Entry: HL = ticks to delay
; Exit: A = result
;
; Replaces the PL/M-generated DELAY by calling BDOS directly
; with proper register setup (avoids ??AUTO storage issues)
;----------------------------------------------------------------------
        PUBLIC  DELAY
DELAY:
        ex      de, hl          ; DE = ticks
        ld      c, 8DH          ; C = XDOS_DELAY function (141)
        ld      hl, (RSPBASE)   ; HL = RSP common module base
        ld      a, (hl)         ; low byte of bdos$entry
        inc     hl
        ld      h, (hl)         ; high byte of bdos$entry
        ld      l, a            ; HL = bdos$entry
        jp      (hl)            ; jump to BDOS (C=func, DE=parm)

;----------------------------------------------------------------------
; BDOS - Call BDOS with function and parameter
; Entry: uplm80 calling convention - args pushed on stack
;        Stack: [ret addr] [param low] [param high] [func]
; Exit:  A = result, HL = result (for address returns)
;
; For banked RSPs, BDOS must be called through the bdos$entry address
; stored at offset 0 of the RSP common module (filled by GENSYS).
; We cannot call 0005H directly from bank 0.
;----------------------------------------------------------------------
        PUBLIC  BDOS
BDOS:
        ; Stack on entry: [ret addr] [parm] [func]
        ; Read args from stack without removing them
        ld      hl, 2
        add     hl, sp
        ld      e, (hl)         ; parm low
        inc     hl
        ld      d, (hl)         ; parm high -> DE = parm
        inc     hl
        ld      c, (hl)         ; func -> C = function number
        ; Get bdos$entry address from RSP common module
        push    de              ; save parm
        ld      hl, (RSPBASE)   ; HL = RSP common module base
        ld      a, (hl)         ; get low byte of bdos$entry
        inc     hl
        ld      h, (hl)         ; get high byte of bdos$entry
        ld      l, a            ; HL = bdos$entry
        pop     de              ; restore parm
        ; Re-read function number since we clobbered C
        push    hl              ; save bdos$entry
        ld      hl, 6           ; SP+6 now (after push) = func
        add     hl, sp
        ld      c, (hl)         ; C = function number
        pop     hl              ; HL = bdos$entry
        jp      (hl)            ; jump to bdos$entry (returns to our caller)

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
; GETBUFBYTE - Get byte from SFTPBUF at offset
; Entry: Stack has offset (word) - caller will clean up
; Exit:  A = byte value
;----------------------------------------------------------------------
        PUBLIC  GETBUFBYTE
GETBUFBYTE:
        ld      hl, 2
        add     hl, sp          ; HL points to offset
        ld      e, (hl)         ; E = offset low
        inc     hl
        ld      d, (hl)         ; D = offset high
        ld      hl, SFTPBUF     ; HL = buffer base (gensys.py relocates correctly)
        add     hl, de          ; HL = buffer + offset
        ld      a, (hl)         ; A = byte value
        ret

;----------------------------------------------------------------------
; SETBUFBYTE - Set byte in SFTPBUF at offset
; Entry: Stack has value (word), offset (word) - caller cleans up
;        PL/M pushes in order: offset first, then value
;        So stack is: [ret addr][value][offset]
; Exit:  none
;----------------------------------------------------------------------
        PUBLIC  SETBUFBYTE
SETBUFBYTE:
        ld      hl, 2
        add     hl, sp          ; HL points to value
        ld      c, (hl)         ; C = value low byte
        inc     hl
        inc     hl              ; HL points to offset
        ld      e, (hl)         ; E = offset low
        inc     hl
        ld      d, (hl)         ; D = offset high
        ld      hl, SFTPBUF     ; HL = buffer base (gensys.py relocates correctly)
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
        ld      bc, SFTPBUF     ; BC = SFTPBUF address (gensys.py relocates)
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
; Entry: Stack has trace code (byte) - caller cleans up
; Exit:  none
;----------------------------------------------------------------------
        PUBLIC  SFTPDEBUG
SFTPDEBUG:
        ld      hl, 2
        add     hl, sp          ; HL points to trace code
        ld      c, (hl)         ; C = trace code
        ld      a, 6DH          ; SFTP_DEBUG function code
        out     (0E0H), a       ; Dispatch to XIOS
        ret

;----------------------------------------------------------------------
; COPYFCBNAME - Copy filename and extension from SFTPBUF to FCB
; Entry: Stack has FCB address - caller cleans up
;        SFTPBUF layout:
;          [0]=type [1]=drive [2]=user [3]=flags
;          [4-11]=filename (8 bytes)
;          [12-14]=extension (3 bytes)
; Exit:  FCB is set up with drive, name, and extension
;----------------------------------------------------------------------
        PUBLIC  COPYFCBNAME
COPYFCBNAME:
        ; Get FCB address from stack
        ld      hl, 2
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)         ; DE = FCB address
        ; Get SFTPBUF address
        ld      hl, SFTPBUF     ; HL = buffer base (gensys.py relocates)
        ; Store drive (buf[1] + 1) -> fcb[0]
        inc     hl              ; HL = &buf[1] (drive)
        ld      a, (hl)
        inc     a               ; drive 0 -> 1, 1 -> 2, etc.
        ld      (de), a         ; Store to FCB[0]
        ; Copy filename buf[4-11] -> fcb[1-8]
        inc     hl
        inc     hl
        inc     hl              ; HL = &buf[4] (filename)
        inc     de              ; DE = &fcb[1]
        ld      bc, 8           ; Copy 8 bytes
        ldir
        ; Copy extension buf[12-14] -> fcb[9-11]
        ; HL is now at buf[12], DE at fcb[9]
        ld      bc, 3           ; Copy 3 bytes
        ldir
        ; Clear remaining FCB fields [12-35] to zero
        ; DE is now at fcb[12]
        xor     a
        ld      b, 24           ; 36-12 = 24 bytes
CLRFCB: ld      (de), a
        inc     de
        djnz    CLRFCB
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
