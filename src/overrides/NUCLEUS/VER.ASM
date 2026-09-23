	title	'MP/M II Version & Revision Date'
	name	'ver'
	cseg
@ver:
	public	@ver
;
;  MP/M II Version
;

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

;
	extrn	mpm
startmpm:
	jmp	mpm

	extrn	pdisp
	jmp	pdisp

	extrn	xbdos
	jmp	xbdos

remfl:
	public	remfl
	jmp	$-$

	public	sysdat
sysdat:
	dw	$-$

;  declare Proc$Address$Table (20) address initial (
;    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0);
	public	ProcAddressTable
ProcAddressTable:
	dw	0,0,0,0,0,0,0,0,0,0
	dw	0,0,0,0,0,0,0,0,0,0

copyright:
	db	'COPYRIGHT (C) 1981,'
	db	' DIGITAL RESEARCH '

serial:
; The nucleus DRI shipped is serialized; the sources carry the
; unserialized placeholder the nucleus is assembled with.
	IFDEF	DRISERIAL
	IFDEF	MPM21
	db	000h,014h,001h,000h,009h,02fh
	ELSE
	db	000h,014h,001h,000h,000h,001h
	ENDIF
	ELSE
	db	'654321'
	ENDIF

	public	mpmver
mpmver:
	IFDEF	MPM21
	dw	0121h
	ELSE
	dw	0120h
	ENDIF

	public	ver
ver:
	db	0dh,0ah,0ah
	db	'MP/M II '
	IFDEF	MPM21
	db	'V2.1'
	ELSE
	db	'V2.0'
	ENDIF
	db	0dh,0ah
	IFDEF	MPM21
	db	'Copyright (C) 1982, Digital Research'
	ELSE
	db	'Copyright (C) 1981, Digital Research'
	ENDIF
	db	0dh,0ah
	db	'$'
	IFDEF	MPM21
	db	'02/01/82'
	ELSE
	db	'09/14/81'
	ENDIF

	end	startmpm
