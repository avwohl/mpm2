	title	'MP/M II V2.0  Emulator XIOS'
	cseg
;
; BNKXIOS for MP/M II Emulator
; This XIOS is designed to be intercepted by the emulator.
; Most functions just return - the emulator handles the real work
; by trapping on the PC address.
;
; Assemble with RMAC, link with LINK to create BNKXIOS.SPR
;
false	equ	0
true	equ	not false

;
; Number of consoles supported
;
nmbcns	equ	8		; 8 consoles for SSH users

;
; XDOS function codes
;
poll	equ	131		; XDOS poll function
flagwait equ	132		; XDOS flag wait
flagset	equ	133		; XDOS flag set

;
; XIOS jump vector
;
;	jmp	coldstart	; not used - commonbase at offset 0
	jmp	commonbase
wboot:
	jmp	warmstart	; warm start
	jmp	const		; console status
	jmp	conin		; console character in
	jmp	conout		; console character out
	jmp	list		; list character out
	jmp	rtnempty	; punch not implemented
	jmp	rtnempty	; reader not implemented
	jmp	home		; move head to home
	jmp	seldsk		; select disk
	jmp	settrk		; set track number
	jmp	setsec		; set sector number
	jmp	setdma		; set dma address
	jmp	read		; read disk
	jmp	write		; write disk
	jmp	listst		; list status
	jmp	sectran		; sector translate
	jmp	selmemory	; select memory
	jmp	polldevice	; poll device
	jmp	startclock	; start clock
	jmp	stopclock	; stop clock
	jmp	exitregion	; exit region
	jmp	maxconsole	; maximum console number
	jmp	systeminit	; system initialization
	db	0		; force internal idle dispatch
;	jmp	idle		; idle procedure

;
; Common base - patched by GENSYS
;
commonbase:
	jmp	coldstart
swtuser: jmp	$-$		; switch to user bank
swtsys:  jmp	$-$		; switch to system bank
pdisp:   jmp	$-$		; MP/M dispatcher
xdos:	 jmp	$-$		; XDOS entry
sysdat:  dw	$-$		; system data page address

coldstart:
warmstart:
	mvi	c,0
	jmp	xdos		; system reset, terminate process

;
; Console Status - return 0FFH if char ready, 00H if not
;
const:
	; Emulator intercepts at this address
	; D = console number
	xra	a		; return 0 (no char ready)
	ret

;
; Console Input - return character in A
;
conin:
	; Emulator intercepts at this address
	; D = console number
	xra	a		; return 0
	ret

;
; Console Output - C = character to output
;
conout:
	; Emulator intercepts at this address
	; D = console number, C = character
	ret

;
; List Output - C = character to print
;
list:
	; Emulator intercepts at this address
	ret

;
; List Status - return 0FFH if ready
;
listst:
	mvi	a,0ffh		; always ready
	ret

rtnempty:
	xra	a
	ret

;
; Disk I/O routines - emulator intercepts these
;
home:
	; Move to track 0
	ret

seldsk:
	; Select disk in C
	; Return HL = DPH address, or 0 if error
	mov	a,c
	cpi	4		; support 4 drives
	jnc	selerr
	; Return DPH address (emulator intercepts and returns proper DPH)
	lxi	h,dph0
	ret
selerr:
	lxi	h,0
	ret

settrk:
	; Set track in BC
	ret

setsec:
	; Set sector in BC
	ret

setdma:
	; Set DMA address in BC
	ret

read:
	; Read sector - return A=0 success, A=1 error
	; Emulator intercepts at this address
	xra	a
	ret

write:
	; Write sector - C = deblocking code
	; Return A=0 success, A=1 error
	; Emulator intercepts at this address
	xra	a
	ret

sectran:
	; Translate sector BC using table DE
	; Return HL = physical sector
	mov	h,b
	mov	l,c		; no translation
	ret

;
; Memory bank selection
;
selmemory:
	; BC = address of memory descriptor
	; Emulator handles bank switching
	ret

;
; Device polling
;
polldevice:
	; C = device number
	; Return 0FFH if ready, 00H if not
	xra	a
	ret

;
; Clock control
;
startclock:
	mvi	a,0ffh
	sta	tickn
	ret

stopclock:
	xra	a
	sta	tickn
	ret

;
; Exit region - enable interrupts if not preempted
;
exitregion:
	lda	preemp
	ora	a
	rnz
	ei
	ret

;
; Maximum console number
;
maxconsole:
	mvi	a,nmbcns
	ret

;
; System initialization
;
systeminit:
	; Set up interrupt handler at RST 7 (0038H)
	mvi	a,0c3h		; JP opcode
	sta	0038h
	lxi	h,inthnd
	shld	0039h

	db	0edh,056h	; IM 1 (Z80 instruction)
	ei
	ret

;
; Interrupt handler
;
inthnd:
	push	psw
	push	b
	push	d
	push	h

	mvi	a,0ffh
	sta	preemp		; set preempted flag

	; Check for clock tick
	lda	tickn
	ora	a
	jz	notick
	mvi	c,flagset
	mvi	e,1
	call	xdos		; set flag #1 (tick)
notick:

	; Update 1-second counter
	lxi	h,cnt60
	dcr	m
	jnz	notsec
	mvi	m,60
	mvi	c,flagset
	mvi	e,2
	call	xdos		; set flag #2 (1 sec)
notsec:

	xra	a
	sta	preemp		; clear preempted flag

	pop	h
	pop	d
	pop	b
	pop	psw
	ei
	jmp	pdisp		; dispatch to next process

;
; Data area
;
tickn:	db	0		; tick enable flag
cnt60:	db	60		; 60 Hz counter
preemp:	db	0		; preempted flag

;
; Disk Parameter Headers (minimal - emulator provides real data)
;
dph0:	dw	0		; XLT
	dw	0,0,0		; scratch
	dw	dirbuf		; DIRBUF
	dw	dpb		; DPB
	dw	0		; CSV
	dw	alv0		; ALV

dpb:	dw	64		; SPT
	db	5		; BSH
	db	31		; BLM
	db	1		; EXM
	dw	2039		; DSM
	dw	1023		; DRM
	db	0ffh		; AL0
	db	0ffh		; AL1
	dw	0		; CKS
	dw	2		; OFF

dirbuf:	ds	128
alv0:	ds	256

	db	0		; force last byte

	end
