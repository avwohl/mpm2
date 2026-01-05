$title	('MP/M II V2.0  Loader BDOS Interface')
	name	ldmonx

;/*
;  Copyright (C) 1979,1980,1981
;  Digital Research
;  P.O. Box 579
;  Pacific Grove, CA 93950
;
;  Revised:
;    14 Sept 81 by Thomas Rolander
;
;  Modified for uplm80 stack-based calling convention
;  Wrapper converts stack parameters to register-based BDOS call
;*/

	cseg
	public	ldmon1,ldmon2

LDRBDOS	equ	0d06h		; Loader BDOS entry point

; LDMON1 - BDOS call wrapper (no return value)
; uplm80 pushes: func first, then info, then calls
; Stack on entry: [top] ret_addr, info, func [bottom]
; After push hl: [top] saved_hl, ret_addr, info, func [bottom]
; SP+0=saved_hl, SP+2=ret_addr, SP+4=info, SP+6=func
; Caller will pop 2 words after call, so we must NOT pop them
ldmon1:
	push	hl		; save HL
	lxi	h,4		; offset to info on stack
	dad	sp		; HL = SP + 4 -> points to info
	mov	e,m		; info low
	inx	h
	mov	d,m		; info high (DE = info)
	inx	h
	mov	c,m		; func low (C = function)
	pop	hl		; restore HL
	call	LDRBDOS
	ret

; LDMON2 - BDOS call wrapper (returns byte in L)
; Stack same as LDMON1
ldmon2:
	push	hl		; save HL
	lxi	h,4		; offset to info on stack
	dad	sp		; HL = SP + 4
	mov	e,m		; info low
	inx	h
	mov	d,m		; info high (DE = info)
	inx	h
	mov	c,m		; func low (C = function)
	pop	hl		; restore HL
	call	LDRBDOS
	mov	l,a		; return value in L
	mvi	h,0
	ret

offset	equ	0000h

fcb	equ	005ch+offset
fcb16	equ	006ch+offset
tbuff	equ	0080h+offset
	public	fcb,fcb16,tbuff

	END
