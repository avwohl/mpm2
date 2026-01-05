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
        EXTRN   ENTRY_POINT     ; Entry point (high byte=0, correctly relocated)

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
        ; Save ticks in DE (HL = ticks on entry)
        ex      de, hl          ; DE = ticks
        ; func = 0x8D (XDOS_DELAY = 141)
        ld      c, 8DH          ; C = function number
        ; Call BDOS entry point (in common module)
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
        ; PL/M calling convention: caller pushes args, calls us, then pops to clean up
        ; Stack on entry (after CALL): [ret addr] [parm] [func]
        ; BDOS expects: C = function, DE = parameter
        ;
        ; Read args from stack without removing them
        ; SP+0,1 = return address
        ; SP+2,3 = parm (last pushed)
        ; SP+4,5 = func (first pushed)
        ld      hl, 2
        add     hl, sp
        ld      e, (hl)         ; parm low
        inc     hl
        ld      d, (hl)         ; parm high -> DE = parm
        inc     hl
        ld      c, (hl)         ; func low -> C = function number
        ; DEBUG: print func and parm
        push    bc              ; save func in C
        push    de              ; save parm
        ld      b, 0            ; BC = func
        ld      a, 7EH          ; Debug: BDOS func
        out     (0E0H), a
        pop     bc              ; BC = parm
        ld      a, 7FH          ; Debug: BDOS parm
        out     (0E0H), a
        pop     bc              ; restore func in C
        ; Get bdos$entry address from RSP common module
        push    de              ; save parm (DE)
        ld      hl, (RSPBASE)   ; HL = RSP common module base
        ; DEBUG: print RSPBASE value
        push    bc
        ld      b, h
        ld      c, l
        ld      a, 79H          ; Debug: RSPBASE
        out     (0E0H), a
        pop     bc
        ld      a, (hl)         ; get low byte of bdos$entry
        inc     hl
        ld      h, (hl)         ; get high byte of bdos$entry
        ld      l, a            ; HL = bdos$entry
        ; DEBUG: print bdos$entry value
        push    bc
        ld      b, h
        ld      c, l
        ld      a, 7AH          ; Debug: bdos$entry
        out     (0E0H), a
        pop     bc
        pop     de              ; restore parm
        ; Re-read function number since we clobbered C
        push    hl              ; save bdos$entry
        ld      hl, 6           ; SP+6 now (after push) = func
        add     hl, sp
        ld      c, (hl)         ; C = function number
        pop     hl              ; HL = bdos$entry
        ; Call through bdos$entry
        jp      (hl)            ; jump to bdos$entry (C=func, DE=parm, returns to our caller)

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
;
; IMPORTANT: Uses runtime address calculation to work around GENSYS bug.
; GENSYS only relocates bytes that are 0x00. SFTPBUF is at 0x0AE6 which
; has non-zero high byte (0x0A), so LD HL,SFTPBUF won't be relocated.
; Instead: load ENTRY_POINT (correctly relocated) and add constant offset.
;----------------------------------------------------------------------
GETSFTPBUFADDR:
        ld      hl, ENTRY_POINT         ; This IS relocated (high byte = 0x00)
        ld      de, 0ADEH               ; Offset: SFTPBUF(0AEA) - ENTRY_POINT(000C)
        add     hl, de                  ; HL = actual SFTPBUF address
        ld      b, h                    ; BC = SFTPBUF address
        ld      c, l
        ret

;----------------------------------------------------------------------
; GETBUFBYTE - Get byte from SFTPBUF at offset
; Entry: Stack has offset (word) - caller will clean up
; Exit:  A = byte value
;----------------------------------------------------------------------
        PUBLIC  GETBUFBYTE
GETBUFBYTE:
        ; Stack on entry: [ret addr][offset]
        ; Read offset from stack without removing it (caller cleans up)
        ld      hl, 2
        add     hl, sp          ; HL points to offset
        ld      e, (hl)         ; E = offset low
        inc     hl
        ld      d, (hl)         ; D = offset high (should be 0)
        push    de              ; save offset (GETSFTPBUFADDR clobbers DE!)
        call    GETSFTPBUFADDR  ; HL = buffer base
        ; DEBUG: report buffer base address
        ld      b, h
        ld      c, l
        ld      a, 80H          ; Debug: GETBUFBYTE buffer addr
        out     (0E0H), a
        pop     de              ; restore offset
        add     hl, de          ; HL = buffer + offset
        ; DEBUG: report final address
        push    hl              ; save final address
        ld      b, h
        ld      c, l
        ld      a, 81H          ; Debug: GETBUFBYTE final addr
        out     (0E0H), a
        pop     hl              ; restore final address
        ld      a, (hl)         ; A = byte value
        ; DEBUG: report value read
        push    af
        ld      c, a            ; BC.low = value
        ld      b, 0
        ld      a, 82H          ; Debug: GETBUFBYTE value read
        out     (0E0H), a
        pop     af
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
        ; Stack on entry: [ret addr][value][offset]
        ; Read args from stack without removing them
        ld      hl, 2
        add     hl, sp          ; HL points to value
        ld      c, (hl)         ; C = value low byte
        inc     hl
        inc     hl              ; HL points to offset
        ld      e, (hl)         ; E = offset low
        inc     hl
        ld      d, (hl)         ; D = offset high (should be 0)
        ld      a, c            ; A = value (save value before call)
        push    de              ; save offset (GETSFTPBUFADDR clobbers DE!)
        push    af              ; save value
        call    GETSFTPBUFADDR  ; HL = buffer base (clobbers BC and DE)
        pop     af              ; restore value
        pop     de              ; restore offset
        add     hl, de          ; HL = buffer + offset
        ld      (hl), a         ; Store byte
        ret

;----------------------------------------------------------------------
; SFTPGETREQUEST - Get SFTP request from C++ into shared buffer
; Entry: none
; Exit:  A = 00h success, 0FFh no request
;----------------------------------------------------------------------
        PUBLIC  SFTPGETREQUEST
SFTPGETREQUEST:
        ; DEBUG: trace on entry
        ld      c, 20           ; trace code 20 = entering SFTPGETREQUEST
        ld      a, 6DH          ; SFTP_DEBUG function
        out     (0E0H), a
        call    GETSFTPBUFADDR  ; BC = SFTPBUF address
        ld      a, 63H          ; SFTP_GET function code
        out     (0E0H), a       ; Dispatch to XIOS
        in      a, (0E0H)       ; Get result
        ; DEBUG: dump SP value
        push    af              ; save result
        push    hl
        ld      hl, 0
        add     hl, sp          ; HL = SP
        ld      b, h
        ld      c, l
        ld      a, 77H          ; Debug: dump SP
        out     (0E0H), a
        ; Also dump return address (at SP+4 now after 2 pushes)
        ld      hl, 4
        add     hl, sp
        ld      e, (hl)
        inc     hl
        ld      d, (hl)         ; DE = return address
        ld      b, d
        ld      c, e
        ld      a, 78H          ; Debug: dump return addr
        out     (0E0H), a
        pop     hl
        ; trace code 11 = before ret
        ld      c, 11
        ld      a, 6DH          ; SFTP_DEBUG function
        out     (0E0H), a
        pop     af              ; restore result
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
; SFTPDEBUG - Debug trace output
; Entry: Stack has trace code (byte) - caller cleans up
; Exit:  none
;----------------------------------------------------------------------
        PUBLIC  SFTPDEBUG
SFTPDEBUG:
        ; Stack on entry: [ret addr][trace code]
        ; Read trace code from stack without removing it
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
        call    GETSFTPBUFADDR  ; HL = SFTPBUF address
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
; NOTE: Must use DB/DW to emit actual bytes (DS doesn't emit in PRL format)
;----------------------------------------------------------------------
        PUBLIC  SFTPBUF
SFTPBUF:
        ; 2048-byte buffer for SFTP data (must use DW to emit bytes)
        ; Allows batching: ~60 dir entries (32 bytes each) or 16x 128-byte records
        REPT    1024
        DW      0
        ENDM

; Note: INITSP_STACK has been moved to sftp_brs_header.asm

        END
