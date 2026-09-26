	title	'MP/M II V2.0 Patch Area'
	name	'patch'
	cseg
@patch:
	public	@patch

;/*
;  Copyright (C) 1979,1980,1981
;  Digital Research
;  P.O. Box 579
;  Pacific Grove, CA 93950
;
;  Revised:
;    14 Sept 81 by Thomas Rolander
;    01 Feb  82 for V2.1
;*/

; V2.0 reserves 128 bytes here so that a field patch has somewhere to put
; code.  V2.1 is that field patch: the five routines below are what the
; call sites in MPM, CLI and XDOS branch to, and together they fill the
; reserved area exactly, so every module after this one keeps its address.

patch:

	IFDEF	MPM21

; ***** MP/M 2.1

	extrn	sysdat
	extrn	rlr
	extrn	rlsmx
	extrn	flshbf
	extrn	memfr
	extrn	pdadr
	extrn	fcb
	extrn	md
	extrn	drive
	extrn	chainseg
	extrn	trysysdisk
	extrn	prbrls

;0000h - called from MPM.ASM in place of
;		LXI	H,16H
;		DAD	D
;		MOV	M,B
; The high nibble now carries the system drive (system$data(123)-1)
; instead of a second copy of the TMP number.
	public	pdisksel
pdisksel:
	MOV	A,B
	ANI	0FH
	MOV	B,A
	PUSH	D
	LHLD	sysdat
	LXI	D,007BH
	DAD	D
	POP	D
	MOV	A,M
	SUI	01H
	RAL
	RAL
	RAL
	RAL
	ANI	0F0H
	ORA	B
	LXI	H,0016H
	DAD	D
	MOV	M,A
	RET

;001dh - called from XDOS.ASM in place of
;		call	flshbf
; The MX queues a process owns are released here, on the way out through
; the console buffer flush, rather than in the dispatcher.
	public	flshmx
flshmx:
	LHLD	rlr
	MOV	B,H
	MOV	C,L
	DI
	CALL	rlsmx
	EI
	CALL	flshbf
	RET

;002bh - called from CLI.ASM in place of
;		LHLD	PDADR
; pd(1dh) collects the f1'..f4' attribute bits of the command's FCB, so
; that a program can be loaded with the flags the command line asked
; for.  system$data(96), unassigned in V2.0, says whether to do it.
	public	cliattr
cliattr:
	PUSH	D
	LHLD	sysdat
	LXI	D,0060H
	DAD	D
	MOV	A,M
	LHLD	pdadr
	LXI	D,001DH
	DAD	D
	MVI	M,00H
	ORA	A
	JZ	cliattr9
	LXI	B,0480H
	LXI	D,fcb+1
cliattr1:
	LDAX	D
	INX	D
	ANI	80H
	JZ	cliattr2
	MOV	A,M
	ORA	C
	MOV	M,A
cliattr2:
	MOV	A,C
	RAR
	MOV	C,A
	DCR	B
	JNZ	cliattr1
	MOV	A,M
	ANI	10H
	JZ	cliattr9
	MOV	A,M
	ORI	60H
	MOV	M,A
cliattr9:
	POP	D
	LXI	B,0014H
	LHLD	pdadr
	RET

;006ah - called from CLI.ASM in place of
;		jmp	@18
; remember the drive the command was found on before retrying it on the
; system disk.
	public	clidrive
clidrive:
	MOV	A,B
	STA	drive
	JMP	trysysdisk

;0071h - called from CLI.ASM in place of
;		JMP	PRINTBrlsfile
; 'No such file.' now also gives back the memory segment that was
; reserved for the program that could not be loaded.
	public	clinofile
clinofile:
	CALL	prbrls
	LDA	chainseg
	INR	A
	RZ
	LXI	B,md
	CALL	memfr
	RET

; the patch area is exactly full - a negative count here is the assembler
; telling you that a patch no longer fits.
	ds	128-($-patch)

	ELSE

;0000h-000fh
	dw	0,0,0,0
	dw	0,0,0,0
;0010h-001fh
	dw	0,0,0,0
	dw	0,0,0,0
;0020h-002fh
	dw	0,0,0,0
	dw	0,0,0,0
;0030h-003fh
	dw	0,0,0,0
	dw	0,0,0,0
;0040h-004fh
	dw	0,0,0,0
	dw	0,0,0,0
;0050h-005fh
	dw	0,0,0,0
	dw	0,0,0,0
;0060h-006fh
	dw	0,0,0,0
	dw	0,0,0,0
;0070h-007fh
	dw	0,0,0,0
	dw	0,0,0,0

	ENDIF

	end
