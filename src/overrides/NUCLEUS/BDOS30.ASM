;
;*****************************************************************
;*****************************************************************
;
;	error messages
IF MPM
dskmsg:	db	'Bdos Err On '
dskerr:	db	' : ',0	
permsg:	db	'Bad Sector',0
selmsg:	db	'Select',0
rofmsg:	db	'File '
rodmsg:	db	'R/O',0
ELSE
dskmsg:	db	'Bdos Err On '
dskerr:	db	' : $'	;filled in by errflg
permsg:	db	'Bad Sector$'
selmsg:	db	'Select$'
rofmsg:	db	'File '
rodmsg:	db	'R/O$'
XERROR:	DB	'X/Err '
XERRID:DB	' $'
ENDIF
;
IF BNKBDOS
setlret1:
	mvi a,1
staret:
	sta	aret
funcret:
	ret
entsp:	ds	2
ENDIF
;
;*****************************************************************
;*****************************************************************
;
;	common values shared between bdosi and bdos
usrcode:db	0	;current user number
info:	ds	2	;information address
aret:	ds	2	;address value to return
lret	equ	aret	;low(aret)
;
;*****************************************************************
;*****************************************************************
;**                                                             **
;**   B a s i c    D i s k   O p e r a t i n g   S y s t e m    **
;**                                                             **
*****************************************************************
;*****************************************************************
;
dvers	equ	30h	;version 3.0
;	module addresses
;
;	literal constants
true	equ	0ffh	;constant true
false	equ	000h	;constant false
enddir	equ	0ffffh	;end of directory
byte	equ	1	;number of bytes for "byte" type
word	equ	2	;number of bytes for "word" type
;
;	fixed addresses in low memory
tfcb	equ	005ch	;default fcb location
tbuff	equ	0080h	;default buffer location
;
;	fixed addresses referenced in bios module are
;	pererr (0009), selerr (000c), roderr (000f)
;
;	error message handlers
;
;
roderror:						;
	;report read/only disk error
	MVI B,2
 	JMP GOERR				;
;
roferror:						;
	;report read/only file error			;
	MVI B,3
 	JMP GOERR	
;
selerror:
	;report select error
	MVI B,4
;
goerr:
	;HL = .errorhandler, call subroutine
	MOV H,B! MVI L,0FFH! SHLD ARET
IF MPM
	call testerrormode! jnz rtnphyerrs
ELSE
	LDA ERRORMODE! ORA A! JNZ RTNPHYERRS
ENDIF
	MOV A,B! LXI H,PERERR-2! JMP BDOSJMP
;
RTNPHYERRS:
;
IF MPM
	lda lockshell! ora a! jnz lockperr
ENDIF
	LDA RETURNFFFF! ORA A! JNZ GOBACK0
	JMP GOBACK
;
IF MPM
testerrormode:
	lxi d,pname+4
testerrormode1:
	call rlr! dad d
	mov a,m! ani 80h! ret
ENDIF
;
BDEEBDEMHL:
	MOV A,E! SUB L! MOV E,A ;
	MOV A,D! SBB H! MOV D,A ;
	RNC! DCR B! RET
;
BDEEBDEPHL:
	MOV A,E! ADD L! MOV E,A ;
	MOV A,D! ADC H! MOV D,A ;
	RNC! INR B! RET
;
SHL3BV:
	INR C
SHL3BV1:
	DCR C! RZ
	DAD H! ADC A! JMP SHL3BV1
;
;*********************************************
;	routines used by shell and lock/unlock
;*********************************************
incrrr:
	call getrra
	inr m! rnz
	inx h! inr m! rnz
	inx h! inr m! ret
;
saverr:
	call saverr2! xchg
saverr1:
	mvi c,3! jmp move ;ret
saverr2:
	call getrra! lxi d,saveranr! ret
;
resetrr:
	call saverr2! jmp saverr1 ;ret
;
compare:
	ldax d! cmp m! rnz
	inx h! inx d! dcr c! rz
	jmp compare
;****************************************************
;
;
;	local subroutines for bios interface
;
move:
	;move data length of length C from source DE to
	;destination given by HL
	inr c ;in case it is zero
	move0:
		dcr c! rz ;more to move
		ldax d! mov m,a ;one byte moved
		inx d! inx h ;to next byte
		jmp move0
;
selectdisk:
	;select the disk drive given by curdsk, and fill
	;the base addresses curtrka - alloca, then fill
	;the values of the disk parameter block
	MVI A,0FFH! STA CURDSK ; IN CASE SELECT FAILS
	LDA SELDSK! mov c,a ;current disk# to c
	;lsb of e = 0 if not yet logged - in
	call seldskf ;HL filled by call
	;HL = 0000 if error, otherwise disk headers
	mov a,h! ora l! rz ;return with 0000 in HL and z flag
		;disk header block address in hl
		mov e,m! inx h! mov d,m! inx h ;DE=.tran
		shld cdrmaxa! inx h! inx h ;.cdrmax
		shld curtrka! inx h! inx h ;HL=.currec
		shld curreca! inx h! inx h ;HL=.buffa
		INX H! SHLD DRVLBLA! INX H
		;DE still contains .tran
		xchg! shld tranv ;.tran vector
		LXI H,DPBADDR ;DE= source for move, HL=dest
		mvi c,addlist! call move ;addlist filled
		;now fill the disk parameter block
		lhld dpbaddr! xchg ;DE is source
		lxi h,sectpt ;HL is destination
		mvi c,dpblist! call move ;data filled
		;now set single/double map mode
		lhld maxall ;largest allocation number
		mov a,h ;00 indicates < 255
		lxi h,single! mvi m,true ;assume a=00
		ora a! jz retselect
		;high order of maxall not zero, use double dm
		mvi m,false
	retselect:
	LDA SELDSK! STA CURDSK
IF NOT MPM
	LHLD BUFFA! MOV A,H! ORA L! RNZ
	LHLD DRVLBLA! MOV D,M! DCX H! MOV E,M
	XCHG! SHLD BUFFA
ENDIF
	INR A! RET ;select disk function ok
;
home:
	;move to home position, then offset to start of dir
	call homef ;move to track 00, sector 00 reference
	;lxi h,offset ;mov c,m ;inx h ;mov b,m ;call settrkf	;
	;first directory position selected
	xra a ;constant zero to accumulator
	lhld curtrka! mov m,a! inx h! mov m,a ;curtrk=0000
	lhld curreca! mov m,a! inx h! mov m,a ;currec=0000
	INX H! MOV M,A ;CURREC HIGH BYTE=00
	LXI H,0! SHLD DBLK ; DBLK = 0000
	;curtrk, currec both set to 0000
	ret
;
PASSARECORD:
	LXI H,ARECORD
	MOV E,M! INX H! MOV D,M! INX H! MOV B,M
	RET
;
rdbuff:
	;read buffer and check condition
	CALL PASSARECORD
	call readf ;current drive, track, sector, dma
	jmp diocomp	;check for i/o errors
;
wrbuff:
	;write buffer and check condition
	;write type (wrtype) is in register C
	;wrtype = 0 => normal write operation
	;wrtype = 1 => directory write operation
	;wrtype = 2 => start of new block
	CALL PASSARECORD
IF MPM
	lda remdrv! ora c! mov c,a
ENDIF
	call writef ;current drive, track, sector, dma
diocomp:	;check for disk errors
	ora a! rz			;
	MOV B,A
	CPI 3! JC GOERR
	MVI B,1! JMP GOERR
;
seekdir:
	;seek the record containing the current dir entry
;
	LXI D,0FFFFH ; MASK = FFFF
	LHLD DBLK! MOV A,H! ORA L! JZ SEEKDIR1
	LDA BLKMSK! MOV E,A! XRA A! MOV D,A ; MASK = BLKMSK
	LDA BLKSHF! MOV C,A! XRA A ;
	CALL SHL3BV ; AHL = SHL(DBLK,BLKSHF)
SEEKDIR1:
	PUSH H! PUSH A ; SAVE AHL
;
	lhld dcnt ;directory counter to HL
	mvi c,dskshf! call hlrotr ;value to HL
;
;	ARECORD = SHL(DBLK,BLKSHF) + SHR(DCNT,DSKSHF) & MASK
;
	MOV A,L! ANA E! MOV L,A ; DCNT = DCNT & MASK
	MOV A,H! ANA D! MOV H,A
	POP B! POP D! CALL BDEEBDEPHL ;
	LXI H,ARECORD! MOV M,E! INX H! MOV M,D! INX H! MOV M,B
;
	;  jmp seek				;
	;ret
;
;
seek:
	;seek the track given by arecord (actual record)
;
	LHLD CURTRKA! MOV C,M! INX H! MOV B,M ; BC = CURTRK
	PUSH B ; S0 = CURTRK 
	LHLD CURRECA! MOV E,M! INX H! MOV D,M
	INX H! MOV B,M ; BDE = CURREC
	LHLD ARECORD! LDA ARECORD+2! MOV C,A ; CHL = ARECORD
SEEK0:
	MOV A,L! SUB E! MOV A,H! SBB D! MOV A,C! SBB B
	PUSH H ; SAVE LOW(ARECORD)
	JNC SEEK1 ; IF ARECORD >= CURREC THEN GO TO SEEK1
	LHLD SECTPT! CALL BDEEBDEMHL ; CURREC = CURREC - SECTPT
	POP H! XTHL! DCX H! XTHL ; CURTRK = CURTRK - 1
	JMP SEEK0
SEEK1:
	LHLD SECTPT! CALL BDEEBDEPHL ; CURREC = CURREC + SECTPT
	POP H ; RESTORE LOW(ARECORD)
	MOV A,L! SUB E! MOV A,H! SBB D! MOV A,C! SBB B
	JC SEEK2 ; IF ARECORD < CURREC THEN GO TO SEEK2
	XTHL! INX H! XTHL ; CURTRK = CURTRK + 1
	PUSH H ; SAVE LOW (ARECORD)
	JMP SEEK1
SEEK2:
	XTHL! PUSH H ; HL,S0 = CURTRK, S1 = LOW(ARECORD)
	LHLD SECTPT! CALL BDEEBDEMHL ; CURREC = CURREC - SECTPT
	POP H! PUSH D! PUSH B! PUSH H ; HL,S0 = CURTRK, 
	; S1 = HIGH(ARECORD,CURREC), S2 = LOW(CURREC), 
	; S3 = LOW(ARECORD)
	XCHG! LHLD OFFSET! DAD D! MOV B,H! MOV C,L
	CALL SETTRKF ; CALL BIOS SETTRK ROUTINE
	; STORE CURTRK
	POP D! LHLD CURTRKA! MOV M,E! INX H! MOV M,D
	; STORE CURREC
	POP B! POP D!
	LHLD CURRECA! MOV M,E! INX H! MOV M,D
	INX H! MOV M,B ; CURREC = BDE
	POP B ; BC = LOW(ARECORD), DE = LOW(CURREC)
	MOV A,C! SUB E! MOV C,A ; BC = BC - DE
	MOV A,B! SBB D! MOV B,A ;
;
	lhld tranv! xchg ;BC=sector#, DE=.tran
	call sectran ;HL = tran(sector)
	mov c,l! mov b,h ;BC = tran(sector)
	jmp setsecf ;sector selected
	;ret
;
;	file control block (fcb) constants
empty	equ	0e5h	;empty directory entry
lstrec	equ	127	;last record# on extent
recsiz	equ	128	;record size
fcblen	equ	32	;file control block size
dirrec	equ	recsiz/fcblen	;directory FCBS / record
dskshf	equ	2	;log2(dirrec)
dskmsk	equ	dirrec-1
fcbshf	equ	5	;log2(fcblen)
;
extnum	equ	12	;extent number field
maxext	equ	31	;largest extent number
ubytes	equ	13	;unfilled bytes field
modnum	equ	14	;data module number
;
MAXMOD	equ	64	;LARGEST MODULE NUMBER
;
fwfmsk	equ	80h	;file write flag is high order modnum
namlen	equ	15	;name length
reccnt	equ	15	;record count field
dskmap	equ	16	;disk map field
lstfcb	equ	fcblen-1
nxtrec	equ	fcblen
ranrec	equ	nxtrec+1;random record field (2 bytes)
;
;	reserved file indicators
rofile	equ	9	;high order of first type char
invis	equ	10	;invisible file in dir command
;
;	utility functions for file access
;
dmposition:
	;compute disk map position for vrecord to HL
	lxi h,blkshf! mov c,m ;shift count to C
	lda vrecord ;current virtual record to A
	dmpos0:
		ora a! rar! dcr c! jnz dmpos0
	;A = shr(vrecord,blkshf) = vrecord/2**(sect/block)
	mov b,a ;save it for later addition
	mvi a,8! sub m ;8-blkshf to accumulator
	mov c,a ;extent shift count in register c
	lda extval ;extent value ani extmsk
	dmpos1:
		;blkshf = 3,4,5,6,7, C=5,4,3,2,1
		;shift is 4,3,2,1,0
		dcr c! jz dmpos2
		ora a! ral! jmp dmpos1
	dmpos2:
	;arrive here with A = shl(ext and extmsk,7-blkshf)
	add b ;add the previous shr(vrecord,blkshf) value
	;A is one of the following values, depending upon alloc
	;bks blkshf
	;1k   3     v/8 + extval * 16
	;2k   4     v/16+ extval * 8
	;4k   5     v/32+ extval * 4
	;8k   6     v/64+ extval * 2
	;16k  7     v/128+extval * 1
	ret ;with dm$position in A
;
GETDMA:
	LHLD INFO! LXI D,DSKMAP! DAD D! RET
;
getdm:
	;return disk map value from position given by BC
	CALL GETDMA
	dad b ;index by a single byte value
	lda single ;single byte/map entry?
	ora a! jz getdmd ;get disk map single byte
		mov l,m! mvi h,0! ret ;with HL=00bb
	getdmd:
		dad b ;HL=.fcb(dm+i*2)
		;double precision value returned
		mov e,m! inx h! mov d,m! xchg! ret
;
index:
	;compute disk block number from current fcb
	call dmposition ;0...15 in register A
	mov c,a! mvi b,0! call getdm ;value to HL
	shld arecord! ret
;
allocated:
	;called following index to see if block allocated
	lhld arecord! mov a,l! ora h! ret
;
atran:
	;compute actual record address, assuming index called
;
;	ARECORD = SHL(ARECORD,BLKSHF)
;
	LDA BLKSHF! MOV C,A
	LHLD ARECORD! XRA A! CALL SHL3BV
	SHLD ARECORD! STA ARECORD+2 ; 
;
	SHLD ARECORD1 ; SAVE LOW(ARECORD)
;
;	ARECORD = ARECORD OR (VRECORD AND BLKMSK)
;
	LDA BLKMSK! MOV C,A! LDA VRECORD! ANA C
	LXI H,ARECORD! ORA M! MOV M,A! RET
;
GETATTS:
	;GET VOLATILE ATTRIBUTES STARTING AT F'5
	;INFO LOCATES FCB
	LHLD INFO
	LXI D,5! DAD D ; HL = .FCB(F'5)
	MVI E,01111111B! MVI C,4
GETATTSLOOP:
	MOV A,M! PUSH A
	RAL! MOV A,D! ADC A! MOV D,A
	POP A! ANA E! MOV M,A
	INX H! DCR C! JNZ GETATTSLOOP
	MOV A,D! ADD A! ADD A! ADD A! ADD A! RET
;
GETS1:
	;GET CURRENT S1 FIELD TO A
	CALL GETEXTA! INX H! MOV A,M! RET ;
;
GETRRA:
	;GET CURRENT RAN REC FIELD ADDRESS TO HL
	LHLD INFO! LXI D,RANREC! DAD D ;HL=.FCB(RANREC)
	RET
;
getexta:
	;get current extent field address to HL
	lhld info! lxi d,extnum! dad d ;HL=.fcb(extnum)
	ret
;
GETRCNTA:
	;GET RECCNT ADDRESS TO HL
	LHLD INFO! LXI D,RECCNT! DAD D! RET
;
getfcba:
	;compute reccnt and nxtrec addresses for get/setfcb
	CALL GETRCNTA! xchg ;DE=.fcb(reccnt)
	lxi h,(nxtrec-reccnt)! dad d ;HL=.fcb(nxtrec) 
	ret
;
getfcb:
	;set variables from currently addressed fcb
	call getfcba ;addresses in DE, HL
	mov a,m! sta vrecord ;vrecord=fcb(nxtrec)
	xchg! mov a,m! sta rcount ;rcount=fcb(reccnt)
	call getexta ;HL=.fcb(extnum)
	lda extmsk ;extent mask to a
	ana m ;fcb(extnum) and extmsk
	sta extval
	ret
;
setfcb:
	;place values back into current fcb
	call getfcba ;addresses to DE, HL
	lda seqio
	cpi 02! jnz setfcb1! xra a	;check ranfill	
setfcb1:
 	mov c,a ;=1 if sequential i/o
	lda vrecord! add c! mov m,a ;fcb(nxtrec)=vrecord+seqio
	xchg! lda rcount! mov m,a ;fcb(reccnt)=rcount
	ret
;
hlrotr:
	;hl rotate right by amount C
	inr c ;in case zero
	hlrotr0: dcr c! rz ;return when zero
		mov a,h! ora a! rar! mov h,a ;high byte
		mov a,l! rar! mov l,a ;low byte
		jmp hlrotr0
;
;
computecs:
	;compute checksum for current directory buffer
	mvi c,recsiz ;size of directory buffer
	lhld buffa ;current directory buffer
	xra a ;clear checksum value
	computecs0:
		add m! inx h! dcr c ;cs=cs+buff(recsiz-C)
		jnz computecs0
	ret ;with checksum in A
;
CHKSUMFCB: ; COMPUTE CHECKSUM FOR FCB
	; ADD 1ST 12 BYTES OF FCB + CURDSK + 
	;     HIGH$EXT + XFCB$READ$ONLY + BBH
IF MPM
	lxi h,pdcnt! mov a,m
	inx h! add m ; Add high$ext
ELSE
	LXI H,HIGHEXT! MOV A,M
ENDIF
	INX H! ADD M ; ADD XFCB$READ$ONLY
	INX H! ADD M ; ADD CURDSK
	ADI 0BBH ; ADD 0BBH TO BIAS CHECKSUM
	LHLD INFO! MVI C,12! CALL COMPUTECS0
	; SKIP EXTNUM
	INX H
	; ADD FCB(S1)
	ADD M! INX H
	; SKIP MODNUM
	INX H
	; SKIP FCB(RECCNT)
	; ADD DISK MAP
	INX H! MVI C,16! CALL COMPUTECS0
	ORA A! RET ; Z FLAG SET IF CHECKSUM VALID
;
SETCHKSUMFCB:
	CALL CHKSUMFCB! RZ
	MOV B,A! CALL GETS1
	CMA! ADD B! CMA
	MOV M,A! RET
;
RESETCHKSUMFCB:
	XRA A! STA COMPFCBCKS
	CALL CHKSUMFCB! RNZ
	CALL GETS1! INR M! RET
;
CHEKFCB:
	LDA HIGHEXT
	; IF EXT & 0110$0000B = 0110$0000B THEN
	; SET FCB(0) TO 0 (USER 0)
	CPI 01100000B! JNZ CHEKFCB1
	LHLD INFO! XRA A! MOV M,A ; FCB(0) = 0
CHEKFCB1:
	JMP CHKSUMFCB ;RET
;
CHECKFCB:
IF MPM
	xra a! sta checkfcb4
checkfcb1:
ENDIF
	CALL CHEKFCB! RZ
CHECKFCB2:
;
IF MPM
	ani 0fh! jnz checkfcb3
	lda pdcnt! ora a! jz checkfcb3
	call setsdcnt! sta dontclose
	call close1
	lxi h,lret! inr m! jz checkfcb3
	mvi m,0! call packsdcnt! mvi b,5
	call searcholist! rz
checkfcb3:
;
ENDIF
	POP H ; DISCARD RETURN ADDRESS
IF MPM
checkfcb4:
	nop
ENDIF
	MVI A,10! JMP STARET
;
SETFCBCKSFLAG:
	MVI A,0FFH! STA COMPFCBCKS! RET
;
hlrotl:
 	;rotate the mask in HL by amount in C
 	inr c ;may be zero
 	hlrotl0: dcr c! rz ;return if zero
 		dad h! jmp hlrotl0
;
;
setcdisk:
	;set a "1" value in curdsk position of BC
	LDA SELDSK
SETCDISK1:
	push b ;save input parameter
	mov c,a ;ready parameter for shift
	lxi h,1 ;number to shift
	call hlrotl ;HL = mask to integrate
	pop b ;original mask
	mov a,c! ora l! mov l,a
	mov a,b! ora h! mov h,a ;HL = mask or rol(1,curdsk)
	ret
;
nowrite:
	;return true if dir checksum difference occurred
	lhld rodsk! 
;
TESTVECTOR:
	LDA SELDSK
TESTVECTOR1:
	mov c,a! call hlrotr
	mov a,l! ani 1b! ret ;non zero if curdsk bit on
;
setro:
	;set current disk to read only
	lxi h,rodsk! mov c,m! inx h! mov b,m
	call setcdisk ;sets bit to 1
	shld rodsk
	;high water mark in directory goes to max
	lhld dirmax! inx h! xchg ;DE = directory max	
	lhld cdrmaxa ;HL = .cdrmax
	mov m,e! inx h! mov m,d ;cdrmax = dirmax
	ret
;
checkrodir:
	;check current directory element for read/only status
	call getdptra ;address of element
;
checkrofile:
	;check current buff(dptr) or fcb(0) for r/o status
	CALL ROTEST
	rnc ;return if not set
	jmp roferror ;exit to read only disk message
;
ROTEST:
	LXI D,ROFILE! DAD D
	MOV A,M! RAL! RET ;CARRY SET IF R/O
;
;
checkwrite:
	;check for write protected disk
	call nowrite! rz ;ok to write if not rodsk
	jmp roderror ;read only disk error
;
getdptra:
	;compute the address of a directory element at
	;positon dptr in the buffer
	lhld buffa! lda dptr			;
addh:
	;HL = HL + A
	add l! mov l,a! rnc
	;overflow to H
	inr h! ret
;
;
getmodnum:
	;compute the address of the module number 
	;bring module number to accumulator
	;(high order bit is fwf (file write flag)
	lhld info! lxi d,modnum! dad d ;HL=.fcb(modnum)
	mov a,m! ret ;A=fcb(modnum)
;
clrmodnum:
	;clear the module number field for user open/make
	call getmodnum! mvi m,0 ;fcb(modnum)=0
	ret
;
CLREXT:
	;FCB EXT = FCB EXT & 1FH
	CALL GETEXTA! MOV A,M! ANI 00011111B! MOV M,A!
	RET
;
setfwf:
	call getmodnum ;HL=.fcb(modnum), A=fcb(modnum)
	;set fwf (file write flag) to "1"
	ori fwfmsk! mov m,a ;fcb(modnum)=fcb(modnum) or 80h
	;also returns non zero in accumulator
	ret
;
;
compcdr:
	;return cy if cdrmax > dcnt
	lhld dcnt! xchg ;DE = directory counter
	lhld cdrmaxa ;HL=.cdrmax
	mov a,e! sub m ;low(dcnt) - low(cdrmax)
	inx h ;HL = .cdrmax+1
	mov a,d! sbb m ;hig(dcnt) - hig(cdrmax)
	;condition dcnt - cdrmax  produces cy if cdrmax>dcnt
	ret
;
setcdr:
	;if not (cdrmax > dcnt) then cdrmax = dcnt+1
	call compcdr
	rc ;return if cdrmax > dcnt
	;otherwise, HL = .cdrmax+1, DE = dcnt
	inx d! mov m,d! dcx h! mov m,e
	ret
;
subdh:
	;compute HL = DE - HL
	mov a,e! sub l! mov l,a! mov a,d! sbb h! mov h,a
	ret
;
newchecksum:
	mvi c,true ;drop through to compute new checksum
checksum:
	;compute current checksum record and update the
	;directory element if C=true, or check for = if not
	;drec < chksiz?
	LHLD ARECORD! xchg! lhld chksiz
	MOV A,H! ANI 7FH! MOV H,A ; MASK OFF PERMANENT DRIVE BIT
	call subdh ;DE-HL
	rnc ;skip checksum if past checksum vector size
		;drec < chksiz, so continue
		push b ;save init flag
		call computecs ;check sum value to A
		lhld checka ;address of check sum vector
		xchg
		LHLD ARECORD
		dad d ;HL = .check(drec)
		pop b ;recall true=0ffh or false=00 to C
		inr c ;0ffh produces zero flag
		jz initialcs
IF MPM
		inr c! jz testdircs
ENDIF
			;not initializing, compare
			cmp m ;compute$cs=check(drec)?
			rz ;no message if ok
			;checksum error, are we beyond
			;the end of the disk?
			call compcdr
			rnc ;no message if so
IF MPM
			call nowrite! cz flushfile0
ENDIF
			jmp setro ;read/only disk set
IF MPM
		testdircs:
			cmp m! jnz flushfiles
			ret
ENDIF
		initialcs:
			;initializing the checksum
			mov m,a! ret
;
;
wrdir:
	;write the current directory entry, set checksum
	CALL CHECKWRITE
	call newchecksum ;initialize entry
	call setdir ;directory dma
	mvi c,1 ;indicates a write directory operation
	call wrbuff ;write the buffer
        jmp setdata ;to data dma address
	;ret
;
rddir:
	;read a directory entry into the directory buffer
	call setdir ;directory dma
	call rdbuff ;directory record loaded
        ; jmp setdata to data dma address    
	;ret
;
setdata:
	;set data dma address
	lxi h,dmaad! jmp setdma ;to complete the call
;
setdir:
	;set directory dma address
	lxi h,buffa  ;jmp setdma to complete call     
;
setdma:
	;HL=.dma address to set (i.e., buffa or dmaad)
	mov c,m! inx h! mov b,m ;parameter ready
	jmp setdmaf
;
;
IF NOT MPM
dirtouser:
	;copy the directory entry to the user buffer
	;after call to search or searchn by user code
	lhld buffa! xchg ;source is directory buffer
	lhld dmaad ;destination is user dma address
	mvi c,recsiz ;copy entire record
	jmp move
	;ret
ENDIF
;
MAKEFCBINV: ; FLAG FCB AS INVALID
	; RESET FCB WRITE FLAG
	CALL SETFWF
	; SET 1ST TWO BYTES OF DISKMAP TO FFH
	INX H! INX H! MVI A,0FFH! MOV M,A! INX H! MOV M,A
	RET
;
CHKINVFCB: ; CHECK FOR INVALID FCB
	CALL GETDMA! JMP TESTFFFF
;
TSTINVFCB: ; TEST FOR INVALID FCB
	CALL CHKINVFCB! RNZ
	POP H! MVI A,9! JMP STARET! ; LRET = 9
;
endofdir:
	;return zero flag if at end of directory, non zero
	;if not at end (end of dir if dcnt = 0ffffh)
	lxi h,dcnt
TESTFFFF:
	mov a,m ;may be 0ffh
	inx h! cmp m ;low(dcnt) = high(dcnt)?
	rnz ;non zero returned if different
	;high and low the same, = 0ffh?
	inr a ;0ffh becomes 00 if so
	ret
;
setenddir:
	;set dcnt to the end of the directory
	lxi h,enddir! shld dcnt! ret
;
readdir:
	;read next directory entry, with C=true if initializing
	lhld dirmax! xchg ;in preparation for subtract
	lhld dcnt! inx h! shld dcnt ;dcnt=dcnt+1
	;continue while dirmax >= dcnt (dirmax-dcnt no cy)
	call subdh ;DE-HL
	jnc readdir0
		;yes, set dcnt to end of directory
		jmp setenddir			;
;		ret				;
	readdir0:
		;not at end of directory, seek next element
		;initialization flag is in C
		lda dcnt! ani dskmsk ;low(dcnt) and dskmsk
		mvi b,fcbshf ;to multiply by fcb size
		readdir1:
			add a! dcr b! jnz readdir1
		;A = (low(dcnt) and dskmsk) shl fcbshf
		sta dptr ;ready for next dir operation
		ora a! rnz ;return if not a new record
		push b ;save initialization flag C
		call seekdir ;seek proper record
		call rddir ;read the directory record
		pop b ;recall initialization flag
		jmp checksum ;checksum the directory elt
		;ret
;
;
getallocbit:
	;given allocation vector position BC, return with byte
	;containing BC shifted so that the least significant
	;bit is in the low order accumulator position.  HL is
	;the address of the byte for possible replacement in
	;memory upon return, and D contains the number of shifts
	;required to place the returned value back into position
	mov a,c! ani 111b! inr a! mov e,a! mov d,a
	;d and e both contain the number of bit positions to shift
;
	MOV H,B! MOV L,C! MVI C,3 ; BC = BC SHR 3
	CALL HLROTR ; HLROTR DOES NOT TOUCH D AND E
	MOV B,H! MOV C,L
;
	lhld alloca ;base address of allocation vector
	dad b! mov a,m ;byte to A, hl = .alloc(BC shr 3)
	;now move the bit to the low order position of A
	rotl: rlc! dcr e! jnz rotl! ret
;
;
setallocbit:
	;BC is the bit position of ALLOC to set or reset.  The
	;value of the bit is in register E.
	push d! call getallocbit ;shifted val A, count in D
	ani 11111110b ;mask low bit to zero (may be set)
	pop b! ora c ;low bit of C is masked into A
;	jmp rotr ;to rotate back into proper position	
	;ret
rotr:
	;byte value from ALLOC is in register A, with shift count
	;in register C (to place bit back into position), and
	;target ALLOC position in registers HL, rotate and replace
	rrc! dcr d! jnz rotr ;back into position
	mov m,a ;back to ALLOC
	ret
;
scandm:
	;scan the disk map addressed by dptr for non-zero
	;entries, the allocation vector entry corresponding
	;to a non-zero entry is set to the value of C (0,1)
	call getdptra ;HL = buffa + dptr
	;HL addresses the beginning of the directory entry
	lxi d,dskmap! dad d ;hl now addresses the disk map
	push b ;save the 0/1 bit to set
	mvi c,fcblen-dskmap+1 ;size of single byte disk map + 1
	scandm0:
		;loop once for each disk map entry
		pop d ;recall bit parity
		dcr c! rz ;all done scanning?
		;no, get next entry for scan
		push d ;replace bit parity
		lda single! ora a! jz scandm1
			;single byte scan operation
			push b ;save counter
			push h ;save map address
			mov c,m! mvi b,0 ;BC=block#
			jmp scandm2
		scandm1:
			;double byte scan operation
			dcr c ;count for double byte
			push b ;save counter
			mov c,m! inx h! mov b,m ;BC=block#
			push h ;save map address
		scandm2:
			;arrive here with BC=block#, E=0/1
			mov a,c! ora b ;skip if = 0000
			jz scanm3
				lhld maxall	;check invalid index
			mov a,l! sub c! mov a,h! sbb b	;maxall - block#
			cnc setallocbit			;
			;bit set to 0/1
		scanm3:						;
			pop h! inx h ;to next bit position
			pop b ;recall counter
			jmp scandm0 ;for another item
;
GETNALBS: ; GET # OF ALLOCATION VECTOR BYTES
	LHLD MAXALL! MVI C,3
	;number of bytes in alloc vector is (maxall/8)+1
	CALL HLROTR! INX H! RET
;
IF MPM
testdir:
	call home
	call setenddir
testdir1:
	mvi c,0feh! call readdir
	lda flushed! ora a! rnz
	call endofdir! rz
	jmp testdir1
;
ENDIF
initialize:
	;initialize the current disk
	;lret = false ;set to true if $ file exists
	;compute the length of the allocation vector - 2
IF MPM
	lhld tlog! call testvector! jz initialize1
	lhld tlog! call removedrive! shld tlog
	xra a! sta flushed
	call testdir! rz
initialize1:
ENDIF
	CALL GETNALBS ; get # of allocation vector bytes
	mov b,h! mov c,l ;count down BC til zero
	lhld alloca ;base of allocation vector
	;fill the allocation vector with zeros
	initial0:
		mvi m,0! inx h ;alloc(i)=0
		dcx b ;count length down
		mov a,b! ora c! jnz initial0
	;
	LHLD DRVLBLA! MOV M,A ; ZERO OUT DRIVE DESC BYTE
	;
	;set the reserved space for the directory
	lhld dirblk! xchg
	lhld alloca ;HL=.alloc()
	mov m,e! inx h! mov m,d ;sets reserved directory blks
	;allocation vector initialized, home disk
	call home
        ;cdrmax = 3 (scans at least one directory record)
	lhld cdrmaxa! mvi m,3! inx h! mvi m,0
	;cdrmax = 0000
	call setenddir ;dcnt = enddir
	;read directory entries and check for allocated storage
	initial2:
		mvi c,true! call readdir
		call endofdir! rz ;return if end of directory
		;not end of directory, valid entry?
		call getdptra ;HL = buffa + dptr
		mvi a,empty! cmp m
		jz initial2 ;go get another item
		;
		MVI A,20H! CMP M! JZ DRVLBL
		MVI A,70H! ANA M! JNZ INITIAL3
		;
		;now scan the disk map for allocated blocks
		mvi c,1 ;set to allocated
		call scandm
	INITIAL3:
		call setcdr ;set cdrmax to dcnt
		jmp initial2 ;for another entry
;
DRVLBL:
		LXI D,EXTNUM! DAD D! MOV A,M
		LHLD DRVLBLA! MOV M,A! JMP INITIAL3
;
;
copydirloc:
	;copy directory location to lret following
	;delete, rename, ... ops
	lda dirloc! jmp staret				;
;	ret						;
;
compext:
	;compare extent# in A with that in C, return nonzero
	;if they do not match
	push b ;save C's original value
	push psw! lda extmsk! cma! mov b,a
	;B has negated form of extent mask
	mov a,c! ana b! mov c,a ;low bits removed from C
	pop psw! ana b ;low bits removed from A
	sub c! ani maxext ;set flags
	pop b ;restore original values
	ret
;
GETDIREXT:
	;COMPUTE DIRECTORY EXTENT FROM FCB
	;SCAN FCB DISK MAP BACKWARDS
	CALL GETFCBA ; HL = .FCB(VRECORD)
	MVI C,16! MOV B,C! INR C! PUSH B
	; B=DSKMAP POS (REL TO 0)
GETDE0:
	POP B ;
	DCR C ;
	XRA A ; COMPARE TO ZERO
GETDE1:
	DCX H! DCR B; DECR DSKMAP POSITION
	CMP M! JNZ GETDE2 ; FCB(DSKMAP(B)) ~= 0
	DCR C! JNZ GETDE1
	;ALL BLOCKS = 0 IN FCB DISK MAP
GETDE2:
	LDA SINGLE! ORA A! MOV A,B ;
	JNZ GETDE3
	RAR ; NOT SINGLE, DIVIDE BLK IDX BY 2
GETDE3:
	PUSH B! PUSH H ; SAVE DSKMAP POSITION & COUNT
	MOV L,A! MVI H,0 ; HL = NON-ZERO BLK IDX
	; COMPUTE EXT OFFSET FROM LAST NON-ZERO
	; BLOCK INDEX BY SHIFTING BLK IDX RIGHT
	; 7 - BLKSHF
	LDA BLKSHF! MOV D,A! MVI A,7! SUB D ;
	MOV C,A! CALL HLROTR! MOV B,L ;
	; B = EXT OFFSET
	LDA EXTMSK! CMP B! POP H! JC GETDE0
	; VERIFY COMPUTED EXTENT OFFSET <= EXTMSK
	CALL GETEXTA! MOV C,M
	CMA! ANI MAXEXT! ANA C! ORA B
	;DIR EXT = (FCB EXT & (~ EXTMSK) & MAXEXT) | EXT OFFSET
	POP B ; RESTORE STACK
	RET ; A = DIRECTORY EXTENT
;
SEARCHI:
	;SEARCH INITIALIZATION
	lhld info! shld searcha ;searcha = info
SEARCHI1:
	mvi a,0ffh! sta dirloc ;changed if actually found
	lxi h,searchl! mov m,c ;searchl = C
	RET
;
search:
	;search for directory element of length C at info
	CALL SEARCHI;
SEARCH1: ; ENTRY POINT USED BY RENAME
	call setenddir ;dcnt = enddir
	call home ;to start at the beginning
	;(drop through to searchn)			;
;
searchn:
	;search for the next directory element, assuming
	;a previous call on search which sets searcha and
	;searchl
;
IF MPM
	lxi h,user0pass! xra a! cmp m! mov m,a! cnz swap
ELSE
	XRA A! STA USER0PASS ;
ENDIF
;
	mvi c,false! call readdir ;read next dir element
	call endofdir! jz searchfin ;skip to end if so
		;not end of directory, scan for match
		lhld searcha! xchg ;DE=beginning of user fcb
		ldax d ;first character
		cpi empty ;keep scanning if empty
		jz searchnext
		;not empty, may be end of logical directory
		push d ;save search address
		call compcdr ;past logical end?
		pop d ;recall address
		jnc searchfin ;artificial stop
searchnext:
		call getdptra ;HL = buffa+dptr
		lda searchl! mov c,a ;length of search to c
		mvi b,0 ;b counts up, c counts down
		;
		MOV A,M! CPI EMPTY! CZ SAVEDCNTPOS1 
		XRA A! STA SAVEXFCB
		MOV A,M! ANI 11101111B! CMP M! JZ SEARCHLOOP
		XCHG! CMP M! XCHG! JNZ SEARCHLOOP
		LDA FINDXFCB! ORA A! JZ SEARCHN
		STA SAVEXFCB! JMP SEARCHOK
		;
		searchloop:
			mov a,c! ora a! jz endsearch
			ldax d! cpi '?'! jz searchok ;? IN USER FCB
			;scan next character if not ubytes
			mov a,b! cpi ubytes! jz searchok
			;not the ubytes field, extent field?
			cpi extnum ;may be extent field
			jz searchext ;skip to search extent
			CPI MODNUM! LDAX D! CZ SEARCHMOD
			sub m! ani 7fh ;mask-out flags/extent modulus
			JNZ SEARCHNM ;skip if not matched
			jmp searchok ;matched character
		searchext:
			LDAX D
			;attempt an extent # match
			push b ;save counters
			;
IF MPM
			push h
 			lhld sdcnt
 			inr h! jnz dontsave
 			lhld dcnt! shld sdcnt
 			lhld dblk! shld sdblk
 		dontsave:
			pop h
ENDIF
			mov c,m ;directory character to c
			call compext ;compare user/dir char
			;
			MOV B,A
			LDA USER0PASS! INR A! JZ SAVEDCNTPOS2
			; DISABLE SEARCH OF USER 0 IF ANY FCB
			; IS FOUND UNDER THE CURRENT USER #
			XRA A! STA SEARCHUSER0
			DCR A! STA FCBEXISTS
			MOV A,B
			;
			pop b ;recall counters
			ORA A ; SET FLAG
			jnz searchn ;skip if no match
		searchok:
			;current character matches
			inx d! inx h! inr b! dcr c
			jmp searchloop
		endsearch:
			;entire name matches, return dir position
			LDA SAVEXFCB! ORA A! JZ ENDSEARCH1
			LDA XDCNT+1! CPI 0FEH! CZ SAVEDCNTPOS0
			JMP SEARCHN
		ENDSEARCH1:
			STA DIRLOC ; DIRLOC = 0
			lda dcnt! ani dskmsk! sta lret
			;lret = low(dcnt) and 11b
			;SUCCESSFUL SEARCH -
			;RETURN WITH ZERO FLAG RESET
			MOV B,A! INR B! RET
		SEARCHMOD:
			ANI 3FH! RET ; MASK OFF HIGH 2 BITS
		searchfin:
			;end of directory, or empty name
			;
			CALL SAVEDCNTPOS1 ;
			;
			;SET DCNT = 0FFFFH
			call setenddir ;may be artifical end
		LRETEQFF:
			;UNSUCCESSFUL SEARCH -
			;RETURN WITH ZERO FLAG SET
			;LRET,LOW(ARET) = 0FFH
			mvi a,255! MOV B,A! INR B! jmp staret		;
			;
		SEARCHNM: ; SEARCH NO MATCH ROUTINE
			MOV A,B! ORA A! JNZ SEARCHN ; FCB(0)?
			MOV A,M! ORA A! JNZ SEARCHN ; DIR FCB(0)=0?
			LDA SEARCHUSER0! ORA A! JZ SEARCHN
			STA USER0PASS
			;
IF MPM
			call swap
ENDIF
			;
			JMP SEARCHOK
;
IF MPM
swap: ; swap dcnt,sdblk with sdcnt0,sdblk0
	push h! push d! push b
	lxi d,sdcnt! lxi h,sdcnt0
	mvi b,4
swap1:
	ldax d! mov c,a! mov a,m
	stax d! mov m,c
	inx h! inx d! dcr b! jnz swap1
	pop b! pop d! pop h! 
	ret
ENDIF
;
SAVEDCNTPOS2:
	; SAVE DIRECTORY POSITION OF MATCHING FCB
	; UNDER USER 0 WITH MATCHING EXTENT # & MODNUM = 0
	; A = 0 ON ENTRY
	ORA B! POP B! LXI B,SEARCHN! PUSH B! RNZ
	; CALL IF USER0PASS = 0FFH &
	;         DIR FCB(EXTNUM) = FCB(EXTNUM)
	;         DIR FCB(MODNUM) = 0
SAVEDCNTPOS0:
	CALL SAVEDCNTPOS ; RETURN TO SEARCHN
SAVEDCNTPOS1:
	; SAVE DIRECTORY POSITION OF FIRST EMPTY FCB
	; OR THE END OF THE DIRECTORY
	PUSH H! 
	LHLD XDCNT! INR H! JNZ SAVEDCNTPOSRET ; RETURN IF H ~= 0FFH
SAVEDCNTPOS:
	LHLD DCNT! SHLD XDCNT ;
	LHLD DBLK! SHLD XDBLK ;
SAVEDCNTPOSRET:
	POP H! RET
;
INITXFCBSEARCH:
	MVI A,0FFH
INITXFCBSEARCH1:
	STA FINDXFCB! MVI A,0FEH! STA XDCNT+1! RET

;
DOESXFCBEXIST:
	LDA XDCNT+1! CPI 0FEH! RZ
	CALL SETDCNTDBLK
	XRA A! CALL INITXFCBSEARCH1
	LHLD SEARCHA! MOV A,M! ORI 10H! MOV M,A
	MVI C,EXTNUM! CALL SEARCHI1! JMP SEARCHN 
;
delete:
	;delete the currently addressed file
	call checkwrite ;write protected?
	CALL GETATTS! STA ATTRIBUTES
	CALL CHKPWS! PUSH A
	CALL INITXFCBSEARCH
	MVI C,EXTNUM! CALL SEARCH ;SEARCH THROUGH FILE TYPE
	POP B! JZ DELETE2
	MOV A,B! ORA A! JNZ CHKPW1
	LDA ATTRIBUTES! RAL! JC DELETE015
	LHLD INFO! CALL CHKWILD! JZ DELETE015
IF MPM
	call tstolist
ENDIF
	CALL CHECKRODIR! JMP DELETE0
;
	DELETE01:
		;LOOP WHILE DIRECTORY MATCHES
		JZ DELETE02
	DELETE015:
IF MPM
		call tstolist
ENDIF
		CALL CHECKRODIR
		CALL SEARCHN! JMP DELETE01
	DELETE02:
;
	LDA ATTRIBUTES! RAL! JC DELETE2

	mvi c,extnum! call search ;search through file type
	delete0:
		;loop while directory matches
		JZ DELETE2
	DELETE1:
		;set each non zero disk map entry to 0
		;in the allocation vector
IF MPM
		call chkolist
ENDIF
		call getdptra ;HL=.buff(dptr)
		MOV A,M! ANI 10H
		mvi m,empty
		mvi c,0! CZ SCANDM ;alloc elts set to 0
		call wrdir ;write the directory
		call searchn ;to next element
		jmp delete0 ;for another record
	DELETE2:
		CALL DOESXFCBEXIST! JNZ DELETE1! JMP COPYDIRLOC
;
getblock:
	;given allocation vector position BC, find the zero bit
	;closest to this position by searching left and right.
	;if found, set the bit to one and return the bit position
	;in hl.  if not found (i.e., we pass 0 on the left, or
	;maxall on the right), return 0000 in hl
	mov d,b! mov e,c ;copy of starting position to de
	lefttst:
		mov a,c! ora b! jz righttst ;skip if left=0000
		;left not at position zero, bit zero?
		dcx b! push d! push b ;left,right pushed
		call getallocbit
		rar! jnc retblock ;return block number if zero
		;bit is one, so try the right
		pop b! pop d ;left, right restored
	righttst:
		lhld maxall ;value of maximum allocation#
		mov a,e! sub l! mov a,d! sbb h ;right=maxall?
		jnc retblock0 ;return block 0000 if so
		inx d! push b! push d ;left, right pushed
		mov b,d! mov c,e ;ready right for call
		call getallocbit
		rar! jnc retblock ;return block number if zero
		pop d! pop b ;restore left and right pointers
		jmp lefttst ;for another attempt
	retblock:
		ral! inr a ;bit back into position and set to 1
		;d contains the number of shifts required to reposition
		call rotr ;move bit back to position and store
		pop h! pop d ;HL returned value, DE discarded
		ret
	retblock0:
		;cannot find an available bit, return 0000
		mov a,c				;
		ora b! jnz lefttst	;also at beginning    
		lxi h,0000h! ret
;
copydir:
	;copy fcb information starting at C for E bytes
	;into the currently addressed directory entry
	MVI D,80H
COPYDIR0:
	CALL COPYDIR2
	INR C
COPYDIR1:
	DCR C! JZ SEEKCOPY
	MOV A,M! ANA B! PUSH B
	MOV B,A! LDAX D! ANI 7FH! ORA B! MOV M,A
	POP B! INX H! INX D! JMP COPYDIR1
COPYDIR2:
	push d ;save length for later
	mvi b,0 ;double index to BC
	lhld info ;HL = source for data
	dad b! xchg ;DE=.fcb(C), source for copy
	call getdptra ;HL=.buff(dptr), destination
	pop b ;DE=source, HL=dest, C=length
	RET
seekcopy:
	;enter from close to seek and copy current element
	call seekdir ;to the directory element
	jmp wrdir ;write the directory element
	;ret
;
CHECKWILD:
	;CHECK FOR ? IN FILE NAME OR TYPE
	LHLD INFO
CHECKWILD0:	; ENTRY POINT USED BY RENAME
	CALL CHKWILD! RNZ
	MVI A,9! POP H ; DISCARD RETURN ADDRESS
	JMP SETARET
;
CHKWILD:
	MVI B,03FH! MVI C,11
CHKWILD1:
	INX H! MOV A,B! SUB M! ANA B! RZ
	DCR C! JNZ CHKWILD1! ORA A! RET
;
COPYUSERNO:
	LHLD INFO! MOV A,M! LXI B,DSKMAP
	DAD B! MOV M,A! RET
;
rename:
	;rename the file described by the first half of
	;the currently addressed file control block. the
	;new name is contained in the last half of the
	;currently addressed file conrol block.  the file
	;name and type are changed, but the reel number
	;is ignored.  the user number is identical
	;
	;VERIFIES NEW FILE NAME DOES NOT EXIST
	;ALSO VERIFIES THAT NO WILD CHARS EXIST
	;IN EITHER FILENAME
;
	call checkwrite ;may be write protected
	;VERIFY NO WILD CHARS EXIST IN 1ST FILENAME
	CALL CHECKWILD
	CALL CHKPASSWORD
;
	CALL INITXFCBSEARCH
;
	;COPY USER NUMBER TO 2ND FILENAME
	CALL COPYUSERNO
	;VERIFY NEW FILENAME DOES NOT ALREADY EXIST
	SHLD SEARCHA
;
	;VERIFY NO WILD CHARS EXIST IN 2ND FILENAME
	CALL CHECKWILD0
;
	MVI C,EXTNUM! CALL SEARCHI1! CALL SEARCH1
	JNZ FILEEXISTS ; NEW FILENAME EXISTS
	CALL DOESXFCBEXIST! CNZ DELETE1
;
	CALL COPYUSERNO
	CALL INITXFCBSEARCH
	;search up to the extent field
	mvi c,extnum! call search
	rz
	CALL CHECKRODIR ;MAY BE READ-ONLY FILE
IF MPM
	call chkolist
ENDIF
	;copy position 0
	rename0:
		;not end of directory, rename next element
		mvi c,dskmap! mvi e,extnum! call copydir
		;element renamed, move to next
		call searchn
		JNZ RENAME0
	RENAME1:
		CALL DOESXFCBEXIST! JZ COPYDIRLOC
		CALL COPYUSERNO! JMP RENAME0
;
indicators:
	;set file indicators for current fcb
	CALL GETATTS ; CLEAR F5' THROUGH F8'
	mvi c,extnum! call search ;through file type
	RZ
IF MPM
	call chkolist
ENDIF
	indic0:
		;not end of directory, continue to change
		mvi c,0! mvi e,extnum ;copy name
		CALL COPYDIR2! CALL MOVE! CALL SEEKCOPY
		call searchn
		JZ COPYDIRLOC
		jmp indic0
;
open:
	;search for the directory entry, copy to fcb
	mvi c,namlen! call search
OPEN1:
	rz ;return with lret=255 if end
	;not end of directory, copy fcb information
opencopy:
	;(referenced below to copy fcb info)
	CALL SETFWF! MOV E,A! PUSH H! DCX H! DCX H
	MOV D,M! PUSH D ; SAVE EXTENT# & MODULE# WITH FCB WRITE FLAG SET
	call getdptra! xchg ;HL = .buff(dptr)
	lhld info ;HL=.fcb(0)
	mvi c,nxtrec ;length of move operation
	push d ; save .buff(dptr)
	call move ;from .buff(dptr) to .fcb(0)
	;note that entire fcb is copied, including indicators
	pop d! lxi h,extnum! dad d ;HL=.buff(dptr+extnum)
	mov c,m ;C = directory extent number
	; RESTORE MODULE # AND EXTENT #
	POP D! POP H! MOV M,E! DCX H! DCX H! MOV M,D
	;HL = .user extent#, C = dir extent#
	;above move set fcb(reccnt) to dir(reccnt)
	;if fcb ext < dir ext then fcb(reccnt) = fcb(reccnt) | 128
	;if fcb ext = dir ext then fcb(reccnt) = fcb(reccnt)
	;if fcb ext > dir ext then fcb(reccnt) = 0
	;
SETRC: ; HL=.FCB(EXT), C=DIREXT
		MVI B,0
		XCHG! LXI H,(RECCNT-EXTNUM)! DAD D
		LDAX D! CMP C! JZ SETRC2
		MVI A,0! JNC SETRC1
		MVI A,128! MOV B,M
	SETRC1:
		MOV M,A
	SETRC2:
		MOV A,B! STA ACTUALRC
		RET 
;
mergezero:
	;HL = .fcb1(i), DE = .fcb2(i),
	;if fcb1(i) = 0 then fcb1(i) := fcb2(i)
	mov a,m! inx h! ora m! dcx h! rnz ;return if = 0000
	ldax d! mov m,a! inx d! inx h ;low byte copied
	ldax d! mov m,a! dcx d! dcx h ;back to input form
	ret
;
RESTORERC:
;	HL = .FCB(EXTNUM)
;	IF ACTUAL$RC ~= 0 THEN RCOUNT = ACTUAL$RC
	PUSH H
	LDA ACTUALRC! ORA A! JZ RESTORERC1
	LXI D,(RECCNT-EXTNUM)! DAD D
	MOV M,A
RESTORERC1:
	POP H! RET
;
close:
	;locate the directory element and re-write it
	xra a! sta lret ;
IF MPM
	sta dontclose
ENDIF
	call nowrite! rnz ;skip close if r/o disk
	;check file write flag - 0 indicates written
	call getmodnum ;fcb(modnum) in A
	ani fwfmsk! rnz ;return if bit remains set
CLOSE1:
	CALL SETFCBCKSFLAG
	CALL GETEXTA! MOV A,M! STA SAVEEXT
	CALL CLOSEFCB
	CALL GETDPTRA! LXI D,EXTNUM! DAD D! MOV C,M
	CALL GETEXTA! LDA SAVEEXT! MOV M,A
	JMP SETRC

CLOSEFCB:
	CALL GETDIREXT! ; HL = .FCB(EXT)
	MOV B,M! MOV M,A! INR A! SUB B! JNZ CLOSE3
	ORA C! JZ CLOSE3
	PUSH H! LXI D,(RECCNT-EXTNUM)! DAD D
	MOV A,M! ORA A! JNZ CLOSE2
	MVI M,80H
CLOSE2:
	POP H
CLOSE3:
	CALL RESTORERC ; RESTORE FCB(RECCNT)
	mvi c,namlen! call search ;locate file
	rz ;return if not found
	;merge the disk map at info with that at buff(dptr)
	lxi b,dskmap! call getdptra
	dad b! xchg ;DE is .buff(dptr+16)
	lhld info! dad b ;DE=.buff(dptr+16), HL=.fcb(16)
	mvi c,(fcblen-dskmap) ;length of single byte dm
	merge0:
		lda single! ora a! jz merged ;skip to double
		;this is a single byte map
		;if fcb(i) = 0 then fcb(i) = buff(i)
		;if buff(i) = 0 then buff(i) = fcb(i)
		;if fcb(i) <> buff(i) then error
		mov a,m! ora a! ldax d! jnz fcbnzero
			;fcb(i) = 0
			mov m,a ;fcb(i) = buff(i)
		fcbnzero:
		ora a! jnz buffnzero
			;buff(i) = 0
			mov a,m! stax d ;buff(i)=fcb(i)
		buffnzero:
		cmp m! jnz mergerr ;fcb(i) = buff(i)?
		jmp dmset ;if merge ok
	merged:
		;this is a double byte merge operation
		call mergezero ;buff = fcb if buff 0000
		xchg! call mergezero! xchg ;fcb = buff if fcb 0000
		;they should be identical at this point
		ldax d! cmp m! jnz mergerr ;low same?
		inx d! inx h ;to high byte
		ldax d! cmp m! jnz mergerr ;high same?
		;merge operation ok for this pair
		dcr c ;extra count for double byte
	dmset:
		inx d! inx h ;to next byte position
		dcr c! jnz merge0 ;for more
		;end of disk map merge, check record count
		;DE = .buff(dptr)+32, HL = .fcb(32)
		lxi b,-(fcblen-extnum)! dad b! xchg! dad b
		;DE = .fcb(extnum), HL = .buff(dptr+extnum)
		ldax d ;current user extent number
		;if fcb(ext) >= buff(fcb) then
		;buff(ext) := fcb(ext), buff(rec) := fcb(rec)
		cmp m! JNC DMSET0
		XCHG ; UPDATE FCB IN MEMORY FROM DIR FCB
DMSET0:

IF MPM
		push a
ENDIF

		;fcb extent number >= dir extent number
		mov m,a ;buff(ext) = fcb(ext)
		;update directory record count field
		lxi b,(reccnt-extnum)! dad b! xchg! dad b
		;DE=.buff(reccnt), HL=.fcb(reccnt)
IF MPM
		;under mp/m reccnt may have been extended
		;by another process - if extents match, set
		;both reccnt fields to the higher value
		pop a! jnz dmset1
		ldax d! cmp m! jc dmset1
		mov m,a ; fcb(reccnt)=buff(reccnt)
dmset1:
ENDIF
		mov a,m! stax d ;buff(reccnt)=fcb(reccnt)
endmerge:
IF MPM
		lda dontclose! ora a! rnz
ENDIF
		;SET T3' OFF INDICATING FILE UPDATE
		CALL GETDPTRA! LXI D,11! DAD D
		MOV A,M! ANI 7FH! MOV M,A
		CALL SETFWF
		jmp seekcopy ;ok to "wrdir" here - 1.4 compat
	;		ret				;
	mergerr:
		;elements did not merge correctly
		CALL MAKEFCBINV
		lxi h,lret! dcr m ;=255 non zero flag set
	ret
;
SETXDCNT:
	LXI H,0FFFFH! SHLD XDCNT! RET
;
SETDCNTDBLK:
	LHLD XDCNT! MVI A,11111100B! ANA L
	MOV L,A! DCX H! SHLD DCNT ;
	LHLD XDBLK! SHLD DBLK ;
	RET
;
IF MPM
sdcnteqxdcnt:
	lxi h,sdcnt! lxi d,xdcnt! mvi c,4
	jmp move
ENDIF
;
make:
	;create a new file by creating a directory entry
	;then opening the file
;
	LXI H,XDCNT! CALL TESTFFFF! JZ LRETEQFF
	CALL SETDCNTDBLK
;
	call checkwrite ;may be write protected
	lhld info! push h ; save fcb address, look for e5
	lxi h,efcb! shld info ; info = .empty
	mvi c,1! 
;
	CALL SEARCHI! CALL SEARCHN
;
	;zero flag set if no space
	pop h ; recall info address
	shld info ;in case we return here
	rz ;return with error condition 255 if not found
;
	; RETURN EARLY IF MAKING AN XFCB
	LDA MAKEXFCB! ORA A! RNZ
	;clear the remainder of the fcb
	;CLEAR S1 BYTE
	LXI D,13! DAD D! MOV M,D! INX H
	;CLEAR AND SAVE FILE WRITE FLAG OF MODNUM
	MOV A,M! PUSH A! PUSH H! ANI 3FH! MOV M,A! INX H
	mvi c,fcblen-namlen ;number of bytes to fill
	make0:
		MOV M,D! inx h! dcr c! jnz make0
	call setcdr ;may have extended the directory
	;now copy entry to the directory
	MVI C,0! LXI D,FCBLEN! CALL COPYDIR0
	;AND RESTORE THE FILE WRITE FLAG
	POP H! POP A! MOV M,A
	;and set the FCB write flag to "1"
	XRA A! STA ACTUALRC
	jmp setfwf
;
openreel:
	;close the current extent, and open the next one
	;if possible.  RMF is true if in read mode
	CALL GETMODNUM! STA SAVEMOD
	CALL GETEXTA
	MOV A,M! MOV C,A ;
	INR C! CALL COMPEXT ;
	JZ OPENREEL3
	PUSH H! PUSH B
	CALL CLOSE
	POP B! POP H
	LDA LRET! INR A! RZ
	MVI A,MAXEXT! ANA C! MOV M,A; INCR EXTENT FIELD
	JNZ OPENREEL0 ; JMP IF IN SAME MODULE
;
	openmod:
		;extent number overflow, go to next module
		INX H! INX H ;HL=.fcb(modnum)
		inr m ;fcb(modnum)=++1
		;module number incremented, check for overflow
;
		mov a,m! ANI 3FH! ;mask high order bits
;
		JZ OPENRERR ;cannot overflow to zero
		;otherwise, ok to continue with new module
	openreel0:
		CALL SETXDCNT ; RESET XDCNT FOR MAKE
IF MPM
		call setsdcnt
ENDIF
		mvi c,namlen! call search ;next extent found?
		jnz openreel1
			;end of file encountered
			lda rmf! inr a ;0ffh becomes 00 if read
			jz openrerr ;sets lret = 1
			;try to extend the current file
			call make
			;cannot be end of directory
			;call end$of$dir
			jz openrerr ;with lret = 1
IF MPM
			call fixolistitem
ENDIF
			CALL SETFCBCKSFLAG
			jmp openreel2
		openreel1:
			;not end of file, open
			CALL SETFCBCKSFLAG
			call opencopy
		openreel2:
			call getfcb ;set parameters
			xra a! STA VRECORD! jmp staret ;lret = 0	;
;			ret ;with lret = 0
	openrerr:
		CALL GETMODNUM! LDA SAVEMOD! MOV M,A
		CALL GETEXTA! LDA SAVEEXT! MOV M,A
		CALL SETFCBCKSFLAG
		;cannot move to next extent of this file
		call setlret1 ;lret = 1
		jmp setfwf ;ensure that it will not be closed
		;ret
	OPENREEL3:
		MOV M,C ; INCREMENT EXTENT FIELD
		CALL GETDIREXT! MOV C,A
		CALL RESTORERC
		CALL SETRC! JMP OPENREEL2
;
seqdiskread:
	;sequential disk read operation
	mvi a,1! sta seqio
	;drop through to diskread
;
diskread:	;(may enter from seqdiskread)
	CALL TSTINVFCB ; CHECK FOR VALID FCB
	mvi a,true! sta rmf ;read mode flag = true (open$reel)
IF MPM
	sta dontclose
ENDIF
	;read the next record from the current fcb
	call getfcb ;sets parameters for the read
DISKREAD0:
	lda vrecord! lxi h,rcount! cmp m ;vrecord-rcount
	;skip if rcount > vrecord
	jc recordok
IF MPM
		call testdiskfcb! jnz diskread0
		lda vrecord
ENDIF
		;not enough records in the extent
		;record count must be 128 to continue
		cpi 128 ;vrecord = 128?
		JNZ SETLRET1 ;skip if vrecord<>128
		call openreel ;go to next extent if so
		; xra a  sta vrecord ;vrecord=00 (DONE BY OPEN$REEL)
		;now check for open ok
		lda lret! ora a! JNZ SETLRET1 ;stop at eof
	recordok:
		;arrive with fcb addressing a record to read
		call index
		;error 2 if reading unwritten data
		;(returns 1 to be compatible with 1.4)
		call allocated ;arecord=0000?
IF MPM
		jnz recordok1
		call testdiskfcb! jnz diskread0
ENDIF
		JZ SETLRET1
	RECORDOK1:
		;record has been allocated, read it
		call atran ;arecord now a disk address
		call seek ;to proper track,sector
		call rdbuff ;to dma address
		jmp setfcb ;replace parameter	

IF MPM
;
testunlocked:
	lda highext! ani 80h! ret
;
testdiskfcb:
	call testunlocked! rz 
	lda dontclose! ora a! rz
	call close1
testdiskfcb1:
	pop d
	lxi h,lret! inr m! mvi a,11! jz staret
	mvi m,0
	push d
	call getrcnta! mov a,m! sta rcount ; reset rcount
	xra a! sta dontclose
	inr a! ret
ENDIF
;
RESETFWF:
	call getmodnum ;HL=.fcb(modnum), A=fcb(modnum)
	;reset the file write flag to mark as written fcb
	ani (not fwfmsk) and 0ffh ;bit reset
	mov m,a ;fcb(modnum) = fcb(modnum) and 7fh
	RET
;
seqdiskwrite:
	;sequential disk write
	mvi a,1! sta seqio
	;drop through to diskwrite
;
diskwrite:	;(may enter here from seqdiskwrite above)
	mvi a,false! sta rmf ;read mode flag
	;write record to currently selected file
	call checkwrite ;in case write protected
;
	LDA XFCBREADONLY! ORA A
	MVI A,3! JNZ SETARET
;
	LDA HIGHEXT! ANI 01000000B
	; Z FLAG RESET IF R/O MODE
	MVI A,3! JNZ SETARET
;
	lhld info ;HL = .fcb(0)
	call checkrofile ;may be a read-only file
;
	CALL TSTINVFCB ; TEST FOR INVALID FCB
	call getfcb ;to set local parameters
	lda vrecord! cpi lstrec+1 ;vrecord-128
	JC DISKWRITE0
	CALL OPENREEL ;VRECORD = 128, TRY TO OPEN NEXT EXTENT
	LDA LRET! ORA A! RNZ ; NO AVAILABLE FCB
DISKWRITE0:
;
IF MPM
	mvi a,0ffh! sta dontclose
diskwrite1:
ENDIF
	;can write the next record, so continue
	call index
	call allocated
IF MPM
	jz diskwrite2
	;if file is unlocked, verify record is not locked
	;record has to be allocated to be locked
	call testunlocked! jz notunlocked
	call atran! mov c,a
	lda multcnt! mov b,a! push b
	call testlock! pop b
	mov a,c! mvi c,0! push b
	jmp diskwr10
notunlocked:
	inr a
ENDIF
	mvi c,0 ;marked as normal write operation for wrbuff
	jnz diskwr1
IF MPM
	diskwrite2:
		call testdiskfcb! jnz diskwrite1
ENDIF
		;not allocated
		;the argument to getblock is the starting
		;position for the disk search, and should be
		;the last allocated block for this file, or
		;the value 0 if no space has been allocated
		call dmposition
		sta dminx ;save for later
		lxi b,0000h ;may use block zero
		ora a! jz nopblock ;skip if no previous block
			;previous block exists at A
			mov c,a! dcx b ;previous block # in BC
			call getdm ;previous block # to HL
			mov b,h! mov c,l ;BC=prev block#
		nopblock:
			;BC = 0000, or previous block #
			call getblock ;block # to HL
		;arrive here with block# or zero
		mov a,l! ora h! jnz blockok
			;cannot find a block to allocate
			mvi a,2! jmp staret 	;lret=2	
		blockok:
		CALL SETFCBCKSFLAG
		;allocated block number is in HL
		shld arecord
		xchg ;block number to DE
		lhld info! lxi b,dskmap! dad b ;HL=.fcb(dskmap)
		lda single! ora a ;set flags for single byte dm
		lda dminx ;recall dm index
		jz allocwd ;skip if allocating word
			;allocating a byte value
			call addh! mov m,e ;single byte alloc
			jmp diskwru ;to continue
		allocwd:
		;allocate a word value
			mov c,a! mvi b,0 ;double(dminx)
			dad b! dad b ;HL=.fcb(dminx*2)
			mov m,e! inx h! mov m,d ;double wd
		diskwru:
		;disk write to previously unallocated block
		mvi c,2 ;marked as unallocated write
	diskwr1:
	;continue the write operation of no allocation error
	;C = 0 if normal write, 2 if to prev unalloc block
	;lda lret  ora a  rnz ;stop if non zero returned value
	;ABOVE LINE REMOVED - LRET CANNOT BE NON-ZERO
	push b ;save write flag
	call atran ;arecord set
	diskwr10:
;
		MOV D,A ; SAVE LOW BYTE OF ARECORD RETURNED
			; IN REGISTER A FROM ATRAN
;
		lda seqio! dcr a! dcr a! jnz diskwr11   ;
		pop b! push b! mov a,c! dcr a! dcr a	;
		jnz diskwr11		;old allocation  
;
		PUSH D ; SAVE LOW BYTE OF ARECORD ON STACK
;
		lhld buffa! mov d,a	;zero buffa & fill 
	fill0:  mov m,a! inx h! inr d! jp fill0		;
		call setdir! lhld arecord1		;
		mvi c,2					;
	fill1:  shld arecord! push b! call seek! pop b  ;
		call wrbuff	;write fill record	;
		lhld arecord!	;restore last record     
		mvi c,0		;change  allocate flag   
		lda blkmsk! mov b,a! ana l! cmp b!inx h	;
		jnz fill1	;cont until cluster is zeroed
;
		POP A! STA ARECORD ; RESTORE ARECORD LOW BYTE
;
		call setdata ; restore dma
	diskwr11:					;
	call seek ;to proper file position
	pop b ;
;
	LDA VRECORD! MOV B,A ; LOAD AND SAVE VRECORD
;
	push b ;restore/save write flag (C=2 if new block)
;
	LXI H,BLKMSK! ANA M! JZ WRITE ;
	MVI C,0 ; SET C TO ZERO IF VRECORD DOES NOT LOCATE
;		  FIRST RECORD OF NEW BLOCK
WRITE:
;
	call wrbuff ;written to disk
	pop b ;C = 2 if a new block was allocated, 0 if not
	;increment record count if rcount<=vrecord
	MOV A,B! lxi h,rcount! cmp m ;vrecord-rcount
	jc diskwr2
		;rcount <= vrecord
		mov m,a! inr m ;rcount = vrecord+1
IF	MPM
		call testunlocked! jz write1
	
		;for unlocked files 
		;  rcount = rcount & (~ blkmsk) + blkmsk + 1

		lda blkmsk! mov b,a! inr b! cma! mov c,a
		mov a,m! dcr a! ana c! add b! mov m,a
	write1:
ENDIF
		mvi c,2 ;mark as record count incremented
	diskwr2:
	;A has vrecord, C=2 if new block or new record#
	dcr c! dcr c! jnz noupdate
		CALL RESETFWF
IF MPM
		call testunlocked! jz noupdate
		lda rcount! call getrcnta! mov m,a
		call close
		call testdiskfcb1
ENDIF

noupdate:
;
	;SET FILE WRITE FLAG IF RESET
	CALL GETMODNUM! ANI 01000000B! JNZ DISKWRITE3
	MOV A,M! ORI 01000000B! MOV M,A
	;RESET FCB FILE WRITE FLAG TO ENSURE T3' GETS
	;RESET BY THE CLOSE FUNCTION
	CALL RESETFWF
DISKWRITE3:
	jmp setfcb ;replace parameters
	;ret
;
rseek:   
	;random access seek operation, C=0ffh if read mode
	;fcb is assumed to address an active file control block
	;(modnum has been set to 1100$0000b if previous bad seek)
	xra a! sta seqio ;marked as random access operation
rseek1:
	push b ;save r/w flag
	lhld info! xchg ;DE will hold base of fcb
		lxi h,ranrec! dad d ;HL=.fcb(ranrec)
		mov a,m! ani 7fh! push psw ;record number
		mov a,m! ral ;cy=lsb of extent#
		inx h! mov a,m! ral! ani 11111b ;A=ext#
		mov c,a ;C holds extent number, record stacked
;
		MOV A,M! ANI 11110000B! INX H! ORA M ;
		RRC! RRC! RRC! RRC! MOV B,A ; 
		; B HOLDS MODULE #
;
		; CHECK HIGH BYTE OF RAN REC <= 3
		MOV A,M
		ANI 11111100B! POP H! MVI L,6! MOV A,H ;
;
		;produce error 6, seek past physical eod
		jnz seekerr
		;otherwise, high byte = 0, A = sought record
		lxi h,nxtrec! dad d ;HL = .fcb(nxtrec)
		mov m,a ;sought rec# stored away
	;arrive here with B=mod#, C=ext#, DE=.fcb, rec stored
	;the r/w flag is still stacked.  compare fcb values
	;
		;CHECK MODULE # FIRST
		PUSH D! CALL CHKINVFCB! POP D! JZ RANCLOSE
		lxi h,modnum! dad d! mov a,b ;B=seek mod#
		;could be overflow at eof, producing module#
		;of 90H or 10H, so compare all but fwf
		sub m! ani 3fh! JNZ RANCLOSE ;same?
		;MODULE MATCHES, CHECK EXTENT
		lxi h,extnum! dad d
		MOV A,M! CMP C! JZ SEEKOK1 ;EXTENTS EQUAL
		CALL COMPEXT! JNZ RANCLOSE
		;EXTENT IS IN SAME DIRECTORY FCB
		MOV M,C ; FCB(EXT) = C
		CALL GETDIREXT! MOV C,A
		; HL=.FCB(EXT),C=DIR EXT
		CALL RESTORERC
		CALL SETRC
		JMP SEEKOK1
	ranclose:
		push b! push d ;save seek mod#,ext#, .fcb
		call close ;current extent closed
		pop d! pop b ;recall parameters and fill
		mvi l,3 ;cannot close error #3
		lda lret! inr a! jz badseek
		CALL SETXDCNT ; RESET XDCNT FOR MAKE
IF MPM
		call setsdcnt
ENDIF
		lxi h,extnum! dad d! mov m,c ;fcb(extnum)=ext#
		INX H! INX H! MOV A,M! ANI 040H! ORA B! MOV M,A
		;fcb(modnum)=mod#
		call open ;is the file present?
		lda lret! inr a! jnz seekok ;open successful?
		;cannot open the file, read mode?
		pop b ;r/w flag to c (=0ffh if read)
		push b ;everyone expects this item stacked
		mvi l,4 ;seek to unwritten extent #4
		inr c ;becomes 00 if read operation
		jz badseek ;skip to error if read operation
		;write operation, make new extent
		call make
		mvi l,5 ;cannot create new extent #5
		jz badseek ;no dir space
IF MPM
		call fixolistitem
ENDIF
		;file make operation successful
	seekok:
		CALL SETFCBCKSFLAG
	SEEKOK1:
		pop b ;discard r/w flag
		xra a! jmp staret 	;with zero set	
	badseek:
		;fcb no longer contains a valid fcb, mark
		;with 1100$000b in modnum field so that it
		;appears as overflow with file write flag set
		push h ;save error flag
		CALL SETFCBCKSFLAG
		CALL MAKEFCBINV ; FLAG FCB AS INVALID
		pop h ;and drop through
	seekerr:
		pop b ;discard r/w flag
		mov a,l! sta lret ;lret=#, nonzero
		;setfwf returns non-zero accumulator for err
		RET ; FOLLOWING LINE REMOVED
;		jmp setfwf ;flag set, so subsequent close ok
		;ret
;
randiskread:
	;random disk read operation
	mvi c,true ;marked as read operation
	call rseek
	cz diskread ;if seek successful
	ret
;
randiskwrite:
	;random disk write operation
	mvi c,false ;marked as write operation
	call rseek
	cz diskwrite ;if seek successful
	ret
;
computerr:
	;compute random record position for getfilesize/setrandom
	xchg! dad d
	;DE=.buf(dptr) or .fcb(0), HL = .f(nxtrec/reccnt)
	mov c,m! mvi b,0 ;BC = 0000 0000 ?rrr rrrr
	lxi h,extnum! dad d! mov a,m! rrc! ani 80h ;A=e000 0000
	add c! mov c,a! mvi a,0! adc b! mov b,a
	;BC = 0000 000? errrr rrrr
	mov a,m! rrc! ani 0fh! add b! mov b,a
	;BC = 000? eeee errrr rrrr
	lxi h,modnum! dad d! mov a,m ;A=XXmm mmmm
	add a! add a! add a! add a ;cy=m A=mmmm 0000
;
	ORA A! ADD B! MOV B,A! PUSH PSW ; SAVE CARRY
	MOV A,M! RAR! RAR! RAR! RAR! ANI 00000011B ; A=0000 00mm
	MOV L,A! POP PSW! MVI A,0! ADC L ; ADD CARRY
	RET ;
;
getfilesize:
	;compute logical file size for current fcb
	;zero the receiving ranrec field
	CALL GETRRA! push h ;save position
	mov m,d! inx h! mov m,d! inx h! mov m,d;=00 00 00
	MVI C,EXTNUM! CALL SEARCH
	getsize:
		jz setsize
		;current fcb addressed by dptr
		call getdptra! lxi d,reccnt ;ready for compute size
		call computerr
		;A=0000 00mm BC = mmmm eeee errr rrrr
		;compare with memory, larger?
		pop h! push h ;recall, replace .fcb(ranrec)
		mov e,a ;save cy
		mov a,c! sub m! inx h ;ls byte
		mov a,b! sbb m! inx h ;middle byte
		mov a,e! sbb m ;carry if .fcb(ranrec) > directory
		jc getnextsize ;for another try
		;fcb is less or equal, fill from directory
		mov m,e! dcx h! mov m,b! dcx h! mov m,c
	getnextsize:
		call searchn
		MVI A,0! STA ARET
		jmp getsize
	setsize:
	pop h ;discard .fcb(ranrec)
	ret
;
setrandom:
	;set random record from the current file control block
	XCHG! lxi d,nxtrec ;ready params for computesize
	call computerr ;DE=info, A=0000 00mm, BC=mmmm eeee errr rrrr
	lxi h,ranrec! dad d ;HL = .fcb(ranrec)
	mov m,c! inx h! mov m,b! inx h! mov m,a ;to ranrec
	ret
;
TMPSELECT:
	LXI H,SELDSK! MOV A,M! PUSH A! MOV M,E
	CALL CURSELECT! POP A! STA SELDSK! RET
;
XCURSELECT:
	MVI A,0FFH! STA RETURNFFFF 
	CALL CURSELECT
	XRA A! STA RETURNFFFF! RET
;
CURSELECT:
	LDA SELDSK! 
	LXI H,CURDSK! CMP M! JZ TESTFFDSK
	;SKIP IF SELDSK = CURDSK, FALL INTO SELECT
;
select:
	;select disk info for subsequent input or output ops
	lhld dlog! CALL TESTVECTOR
	MOV E,A! PUSH D ;SEND TO SELDSK, SAVE FOR TEST BELOW
	call selectdisk! pop h ;recall dlog vector
	cz selerror ;returns true if select ok
	;is the disk logged in?
	mov a,l! ORA A
IF MPM
	jz select1
	lhld rlog! call testvector
	sta remdrv! ret ; set rem$drv & return
select1:
ELSE
	rnz ;return if bit is set
ENDIF
	;disk not logged in, set bit and initialize
	lhld dlog! mov c,l! mov b,h ;call ready
	call setcdisk! shld dlog ;dlog=set$cdisk(dlog)
IF MPM
	lxi h,chksiz+1! mov a,m! ral! mvi a,0! jc select2
	lhld rlog! mov c,l! mov b,h
	call setcdisk! shld rlog ;rlog=set$cdisk(rlog)
	mvi a,1
select2:
	sta remdrv
ENDIF
	jmp initialize
	;ret
;
TESTFFDSK:
	INR A! JZ SELERROR
	RET
;
SETSELDSK:
	LDA LINFO! STA SELDSK! RET
;
RESELECTX:
	XRA A! STA HIGHEXT! JMP RESELECT1
reselect:
	;check current fcb to see if reselection necessary
	MVI A,80H! MOV B,A! DCR A! MOV C,A
	LHLD INFO! LXI D,7! XCHG! DAD D
	MOV A,M! ANA B! STA XFCBREADONLY
	MOV A,M! ANA C! MOV M,A
	INX H! LXI D,4
	MOV A,M! ANA C! CMP M! MOV M,A! MVI A,60H! JNZ RESELECT0
	DAD D! MVI A,0E0H! ANA M
RESELECT0:
	STA HIGHEXT! CALL CLREXT
	CALL GETRCNTA! MOV A,M! ANA B! JZ RESELECT1
	MOV A,M! ANA C! MOV M,B
RESELECT1:
	STA ACTUALRC
	mvi a,true! sta resel ;mark possible reselect
	lhld info! mov a,m ;drive select code
	ani 11111b ;non zero is auto drive select
	dcr a ;drive code normalized to 0..30, or 255
	sta linfo ;save drive code
	CPI 0FFH! JZ NOSELECT
		;auto select function, save SELDSK
		LDA SELDSK! sta olddsk ;olddsk=SELDSK
		mov a,m! sta fcbdsk ;save drive code
		ani 11100000b! mov m,a ;preserve hi bits
		CALL SETSELDSK
	noselect:
		CALL CURSELECT
		;set user code
		lda usrcode ;0...31
		lhld info! ora m! mov m,a
		ret
;
CHKPASSWORD:
	CALL GETDIRMODE! ANI 80H! JNZ CHKPW! RET
;
GETDIRMODE:
	LHLD DRVLBLA! MOV A,M! RET
;
CHKPW:		; CHECK PASSWORD
	CALL GETXFCB! RZ ; A = XFCB OPTIONS
	DCR A! CNZ CMPPW! RZ
CHKPW1:	; PASSWORD ERROR
	POP H! MVI A,7! JMP SETARET
;
CMPPW:		; COMPARE PASSWORDS
	INX H! MOV B,M
	MOV A,B! ORA A! JNZ CMPPW2
	MOV D,H! MOV E,L! INX H! INX H
	MVI C,9
CMPPW1:
	INX H! MOV A,M! DCR C! RZ
	ORA A! JZ CMPPW1
	CPI 20H! JZ CMPPW1
	XCHG
CMPPW2:
	LXI D,(23-UBYTES)! DAD D! XCHG
	LHLD XDMAAD! MVI C,8
CMPPW3:
	LDAX D! XRA B! CMP M! JNZ CMPPW4
	DCX D! INX H! DCR C! JNZ CMPPW3
	RET
CMPPW4:
	DCX D! DCR C! JNZ CMPPW4
	INX D
IF MPM
	call getdfpwa! inr a! jnz cmppw5
	inr a! ret
cmppw5:
ELSE
	LXI H,DFPASSWORD
ENDIF
	MVI C,8! JMP COMPARE
;
IF MPM
getdfpwa:		;A = FF => no df pwa
	call rlr! lxi b,console! dad b
	mov a,m! cpi 16! mvi a,0ffh! rnc
	mov a,m! add a! add a! add a
	mvi h,0! mov l,a! lxi b,dfpassword! dad b
	ret
ENDIF
;
SETPW:		; SET PASSWORD IN XFCB
	PUSH H ; SAVE .XFCB(EX) 
	LXI B,8 ; B = 0, C = 8
	LXI D,(23-EXTNUM)! DAD D
	XCHG! LHLD XDMAAD
SETPW0:
	XRA A! PUSH A
SETPW1:
	MOV A,M! STAX D! ORA A! JZ SETPW2
	CPI 20H! JZ SETPW2
	INX SP! INX SP! PUSH A
SETPW2:
	ADD B! MOV B,A
	DCX D! INX H! DCR C! JNZ SETPW1
	POP A! ORA B! POP H! JNZ SETPW3
	MOV M,A ; ZERO XFCB(EX) - NO PASSWORD
SETPW3:
	INX D! MVI C,8
SETPW4:
	LDAX D! XRA B! STAX D! INX D! DCR C! JNZ SETPW4
	INX H! RET
;
GETXFCB:
	MVI B,0
GETXFCB0:
	LHLD INFO! MOV A,M! PUSH A
	ORI 010H! MOV M,A! MVI C,EXTNUM
	XRA A! CMP B! JNZ GETXFCB00
	CALL SEARCH! JMP GETXFCB01
GETXFCB00:
	CALL SEARCHN
GETXFCB01:
	MVI A,0! STA LRET
	LHLD INFO! POP B! MOV M,B! RZ
GETXFCB1:
	CALL GETDPTRA! XCHG
	LXI H,EXTNUM! DAD D! MOV A,M! ANI 0E0H! ORI 1
	RET
;
ADJUSTDMAAD:
	PUSH H! LHLD XDMAAD! DAD D
	SHLD XDMAAD! POP H! RET
;
INITXFCB:
	CALL SETCDR ;MAY HAVE EXTENDED THE DIRECTORY
	LXI B,1014H ; B=10H, C=20
INITXFCB0:
	;B = FCB(0) LOGICAL OR MASK
	;C = ZERO COUNT
	PUSH B
	CALL GETDPTRA! XCHG! LHLD INFO! XCHG
	;ZERO EXTNUM AND MODNUM
	LDAX D! ORA B! MOV M,A! INX D! INX H
	MVI C,11! CALL MOVE! POP B! INR C
INITXFCB1:
	DCR C! RZ
	MVI M,0! INX H! JMP INITXFCB1
;
CHKXFCBPASSWORD:
	CALL GETXFCB1! DCR A
CHKXFCBPASSWORD1:
	PUSH H! CNZ CMPPW! POP H! RZ
	JMP CHKPW1
;
CHKPWS:
	CALL GETDIRMODE! ANI 80H! RZ
	CALL GETXFCB
CHKPWS1:
	RZ! DCR A
	CNZ CMPPW! RNZ
	MVI B,1! CALL GETXFCB0! JMP CHKPWS1
;
STAMP1:
	LXI B,24! JMP STAMP3
STAMP2:
	LXI B,28!
STAMP3:
;
IF MPM
	push b!
	call getstampadd! xchg
	pop b
ELSE
	LXI D,STAMP
ENDIF
	CALL GETDPTRA! DAD B
	MVI C,4! JMP MOVE ;RET
;
IF MPM
;
packsdcnt:
;
;packed$dcnt = dblk(low 15 bits) || dcnt(low 9 bits)
;
;	if sdblk = 0 then dblk = shr(sdcnt,blkshf+2)
;		     else dblk = sdblk
;	dcnt = sdcnt & (blkmsk || '11'b)
;
;	packed$dcnt format (24 bits)
;
;	12345678 12345678 12345678
;	23456789 .......1 ........ sdcnt (low 9 bits)
;	........ 9abcdef. 12345678 sdblk (low 15 bits)
;
	lhld sdblk! mov a,h! ora l! jnz packsdcnt1
	lda blkshf! adi 2! mov c,a! lhld sdcnt
	call hlrotr
packsdcnt1:
	dad h! xchg! lxi h,sdcnt! mvi b,1
	lda blkmsk! ral! ora b! ral! ora b
	ana m! sta packeddcnt
	lda blkshf! cpi 7! jnz packsdcnt2
	inx h! mov a,m! ana b! jz packsdcnt2
	mov a,e! ora b! mov e,a
packsdcnt2:
	xchg! shld packeddcnt+1
	ret
;
;
;olist element = link(2) || atts(1) || dcnt(3) || 
;		 pdaddr(2) || opncnt(2)
;
;	link = 0 -> end of list
;
;	atts - 80 - open in locked mode
;	       40 - open in unlocked mode
;	       20 - open in read/only mode
;	       10 - deleted item
;	       0n - drive code (0-f)
;
;	dcnt = packed sdcnt+sdblk
;	pdaddr = process descriptor addr
;	opncnt = # of open calls - # of close calls
;		 olist item freed by close when opncnt = 0
;
;llist element = link(2) || drive(1) || arecord(3) || 
;	         pdaddr(2) || .olist$item(2)
;
;	link = 0 -> end of list
;
;	drive - 0n - drive code (0-f)
;
;	arecord = record number of locked record
;	pdaddr = process descriptor addr
;	.olist$item = address of file's olist item
;
searcholist:
	lxi h,openroot! jmp srchlist0
searchllist:
	lxi h,lockroot! jmp srchlist0
searchnlist:
	lhld curpos
srchlist0:
	shld prvpos
;
;search$olist, search$llist, searchn$list conventions
;
;	B = 0 -> return next item
;	B = 1 -> search for matching drive
; 	B = 3 -> search for matching dcnt
;	B = 5 -> search for matching dcnt + pdaddr
;	if found then z flag is set
;	              prv$pos -> previous list element
;		      cur$pos -> found list element
;		      HL -> found list element
;	else prv$pos -> list element to insert after
;
;	olist and llist are maintained in drive order
;
srchlist1:
	mov e,m! inx h! mov d,m! xchg
	mov a,l! ora h! jz srchlist3
	xra a! cmp b! jz srchlist6
	inx h! inx h!
	lxi d,curdsk! mov a,m! ani 0fh! mov c,a
	ldax d! sub c! jnz srchlist4
	mov a,b! dcr a! jz srchlist5
	mov c,b! push h
	inx d! inx h! call compare
	pop h! jz srchlist5
srchlist2:
	dcx h! dcx h
	shld prvpos! jmp srchlist1
srchlist3:
	inr a! ret
srchlist4:
	jnc srchlist2
srchlist5:
	dcx h! dcx h
srchlist6:
	shld curpos! ret
;
deleteitem: ; HL -> item to be deleted
	di
	push d! push h
	mov e,m! inx h! mov d,m
	lhld prvpos! shld curpos
	;prv$pos.link = delete$item.link
	mov m,e! inx h! mov m,d
	;
	lhld freeroot! xchg
	;free$root = .delete$item
	pop h! shld freeroot
	;delete$item.link = previous free$root
	mov m,e! inx h! mov m,d
	pop d! ei! ret
;
createitem: ; HL -> new item if successful
	     ; Z flag set if no free items
	lhld freeroot! mov a,l! ora h! rz
	push d! push h! shld curpos
	mov e,m! inx h! mov d,m
	;free$root = free$root.link
	xchg! shld freeroot
	;
	lhld prvpos
	mov e,m! inx h! mov d,m
	pop h
	;create$item.link = prv$pos.link
	mov m,e! inx h! mov m,d! dcx h
	xchg! lhld prvpos
	;prv$pos.link = .create$item
	mov m,e! inx h! mov m,d! xchg
	pop d! ret
;
setolistitem:
	; A = attributes
	; HL = olist entry address
	inx h! inx h
	mov b,a! lxi d,curdsk! ldax d! ora b
	mov m,a! inx h! inx d
	mvi c,5! call move
	xra a! mov m,a! inx h! mov m,a! ret
;
setsdcnt:
 	mvi a,0ffh! sta sdcnt+1! ret
;
tstolist:
	mvi a,0c9h! sta chkolist05! jmp chkolist0
chkolist:
	xra a! sta chkolist05
chkolist0:
	lxi d,dcnt! lxi h,sdcnt! mvi c,4! call move
	call packsdcnt! mvi b,3! call searcholist! rnz
	pop d ; pop return address
	inx h! inx h
	mov a,m! ani 80h! jz openx06 
	dcx h! dcx h
	push d! push h
	call comparepds! pop h! pop d! jnz openx06
	push d ;restore return address
chkolist05:
	nop ; tst$olist changes this instr to ret
	call deleteitem! lda pdcnt
chkolist1:
	adi 16! jz chkolist1
	sta pdcnt

	push a! call rlr
	lxi b,pdcntoff! dad b! pop a
	mov m,a

	ret
;
removefiles:	;BC = pdaddr
	lhld curpos! push h
	lhld prvpos! push h
	mov d,b! mov e,c! lxi h,openroot! shld curpos
removefile1:
	mvi b,0! push d! call searchnlist! pop d! jnz removefile2
	lxi b,6! call tsttbllmt! jnz removefile1
	inx h! inx h! mov a,m! ori 10h! mov m,a
	sta deletedfiles
	jmp removefile1
removefile2:
	pop h! shld prvpos
	pop h! shld curpos
	ret
;
deletefiles:
	lxi h,openroot! shld curpos
deletefile1:
	mvi b,0! call searchnlist! rnz
	inx h! inx h! mov a,m! ani 10h! jz deletefile1
	dcx h! dcx h! call removelocks! call deleteitem
	jmp deletefile1
;
flushfiles:
	lxi h,flushed! mov a,m! ora a! rnz
	inr m
flushfile0:
	lxi h,openroot! shld curpos
flushfile1:
	mvi b,1! call searchnlist! rnz
	push h! call removelocks! call deleteitem! pop h
	lxi d,6! dad d! mov e,m! inx h! mov d,m
	lxi h,pdcntoff! dad d! mov a,m! ani 1! jnz flushfile1
	mov a,m! ori 1! mov m,a
	lhld pdaddr! mvi c,2! call compare! jnz flushfile1
	lda pdcnt! adi 10h! sta pdcnt! jmp flushfile1
;
freefiles:
	; free$mode = 1 - remove curdsk files for process
	;	      0 - remove all files for process
	lhld pdaddr! xchg! lxi h,openroot! shld curpos
freefiles1:
	lda freemode! mov b,a
	push d! call searchnlist! pop d! rnz
	lxi b,6! call tsttbllmt! jnz freefiles1
	push h! inx h! inx h! inx h
	call testffff! jnz freefiles2
	call testffff! jz freefiles3
freefiles2:
	mvi a,0ffh! sta incrpdcnt
freefiles3:
	pop h! call removelocks! call deleteitem
	jmp freefiles1
;
removelocks:
	shld fileid
	inx h! inx h! mov a,m! ani 40h! jz removelock3
	push d! lhld prvpos! push h
	lhld fileid! xchg! lxi h,lockroot! shld curpos
removelock1:
	mvi b,0! push d! call searchnlist! pop d
	jnz removelock2
	lxi b,8! call tsttbllmt! jnz removelock1
	call deleteitem
	jmp removelock1
removelock2:
	pop h! shld prvpos! pop d
removelock3:
	lhld fileid! ret
;
tsttbllmt:
	push h! dad b
	mov a,m! inx h! mov h,m
	sub e! jnz tsttbllmt1
	mov a,h! sub d
tsttbllmt1:
	pop h! ret
;
createolistitem:
	mvi b,1! call searcholist
	di
	call createitem! lda attributes! call setolistitem
	ei
	ret
;
countopens:
	xra a! sta opencnt
	lhld pdaddr! xchg! lxi h,openroot! shld curpos
countopen1:
	mvi b,0! push d! call searchnlist! pop d! jnz countopen2
	lxi b,6! call tsttbllmt! jnz countopen1
	lda opencnt! inr a! sta opencnt
	jmp countopen1
countopen2:
	lxi h,openmax! lda opencnt! ret
;
countlocks:
	xra a! sta lockcnt
	xchg! lxi h,lockroot! shld curpos
countlock1:
	mvi b,0! push d! call searchnlist! pop d! rnz
	lxi b,8! call tsttbllmt! jnz countlock1
	lda lockcnt! inr a! sta lockcnt
	jmp countlock1
;
checkfree:
	lda multcnt! mov e,a
	mvi d,0! lxi h,freeroot! shld curpos
checkfree1:
	mvi b,0! push d! call searchnlist! pop d! jnz checkfree2
	inr d! mov a,d! sub e! jc checkfree1
	ret
checkfree2:
	pop h! mvi a,14! jmp staret
;
lock:				;record lock and unlock 
	call reselect! call checkfcb
	call testunlocked
	rz ; file not opened in unlocked mode
	lhld xdmaad! mov e,m! inx h! mov d,m
	xchg! inx h! inx h
	mov a,m! mov b,a! lda curdsk! sub b
	ani 0fh! jnz lock8 ; invalid file id
	mov a,b! ani 40h! jz lock8 ; invalid file id
	dcx h! dcx h! shld fileid
	lda lockunlock! inr a! jnz lock1 ; jmp if unlock
	call countlocks
	lda lockcnt! mov b,a
	lda multcnt! add b! mov b,a
	lda lockmax! cmp b
	mvi a,12! jc staret ; too many locks by this process
	call checkfree
lock1:
	call saverr! lxi h,lock9! push h! lda multcnt
lock2:
	push a! call getlockadd
	lda lockunlock! inr a! jnz lock3
	call testlock
lock3:
	pop a! dcr a! jz lock4
	call incrrr! jmp lock2
lock4:
	call resetrr! lda multcnt
lock5:
	push a! call getlockadd
	lda lockunlock! inr a! jnz lock6
	call setlock! jmp lock7
lock6:
	call freelock
lock7:
	pop a! dcr a! rz
	call incrrr! jmp lock5
lock8:
	mvi a,13! jmp staret ; invalid file id
lock9:
	call resetrr! ret
;
getlockadd:
	lxi h,0! dad sp! shld locksp
	mvi a,0ffh! sta lockshell
	call rseek
	xra a! sta lockshell
	call getfcb
	lhld aret! mov a,l! ora a! jnz lockerr
	call index! call allocated
	lxi h,1! jz lockerr
	call atran! ret
;
lockperr:
	xra a! sta lockshell
	xchg! lhld locksp! sphl! xchg
lockerr:
	pop d ; discard return address
	pop b ; B = mult$cnt-# recs processed
	lda multcnt! sub b
	add a! add a! add a! add a
	ora h! mov h,a! mov b,a
	shld aret! ret
;
testlock:
	call movearecord
	mvi b,3! call searchllist! rnz
	call comparepds! rz
	lxi h,8! jmp lockerr
;
setlock:
	call movearecord
	mvi b,1! call searchllist
	di
	call createitem
	xra a! call setolistitem
	xchg! lhld fileid! xchg
	mov m,d! dcx h! mov m,e
	ei! ret
;
freelock:
	call movearecord
	mvi b,5! call searchllist! rnz
freelock0:
	call deleteitem
	mvi b,5! call searchnlist! rnz
	jmp freelock0
;
comparepds:
	lxi d,6! dad d! xchg
	lxi h,pdaddr! mvi c,2! jmp compare
	;ret
;
movearecord:
	lxi d,arecord! lxi h,packeddcnt
	mvi c,3! jmp move ;ret
;
fixolistitem:
	lxi d,xdcnt! lxi h,sdcnt
	;is xdblk,xdcnt < sdblk,sdcnt
	mvi c,4! ora a!
fixol1:
	ldax d! sbb m! inx h! inx d! dcr c! jnz fixol1
	rnc
	;yes - update olist entry
	call swap! call sdcnteqxdcnt
	lxi h,openroot! shld curpos
	;find file's olist entry
fixol2:
	call swap! call packsdcnt! call swap
	mvi b,3! call searchnlist! rnz
	;update olist entry with new dcnt value
	push h! call packsdcnt! pop h
	inx h! inx h! inx h! lxi d,packeddcnt
	mvi c,3! call move! jmp fixol2
;
hleqhlandde:
	mov a,l! ana e! mov l,a
	mov a,h! ana d! mov h,a
	ret
;
removedrive:
	xchg! lda curdsk! mov c,a! lxi h,1
	call hlrotl
	mov a,l! cma! ana e! mov e,a
	mov a,h! cma! ana d! mov d,a
	xchg! ret
;
diskreset:
	lxi h,0! shld ntlog
	xra a! sta setroflag
	lhld info
intrnldiskreset:
	xchg! lhld openroot! mov a,h! ora l! rz
	xchg! lda curdsk! push a! mvi b,0
dskrst1:
	mov a,l! rar! jc dskrst3
dskrst2:
	mvi c,1! call hlrotr! inr b
	mov a,h! ora l! jnz dskrst1
	pop a! sta curdsk
	lhld ntlog! xchg! lhld tlog
	mov a,l! ora e! mov l,a
	mov a,h! ora d! mov h,a! shld tlog
	inr a! ret
dskrst3:
	push b! push h! mov a,b! sta curdsk
	lhld rlog! call testvector1! push a
	lhld rodsk! lda curdsk! call testvector1! mov b,a
	pop h! lda setroflag! ora b! ora h! sta checkdisk
	lxi h,openroot! shld curpos
dskrst4:
	mvi b,1! call searchnlist! jnz dskrst6
	lda checkdisk! ora a! jz dskrst5
	push h! call comparepds! jz dskrst45
	pop h! xra a! xchg! jmp dskrst6
dskrst45:
	lhld ntlog! mov b,h! mov c,l
	lda curdsk! call setcdisk1! shld ntlog
	pop h! jmp dskrst4
dskrst5:
	lhld info! call removedrive! shld info
	ori 1
dskrst6:
	pop h! pop b! jnz dskrst2

	;error - olist item exists for another process
	;for removable drive to be reset
	pop a! sta curdsk! mov a,b! adi 41h ; A = ASCII drive
	lxi h,6! dad d! mov c,m! inx h! mov b,m ; BC = pdaddr
	push psw! call testerrormode! pop d! jnz dskrst7
	mov a,d

	push b! push psw
	call rlr! lxi d,console! dad d! mov d,m ;D = console #
	lxi b,deniedmsg! call xprint
	pop psw! mov c,a! call conoutx
	mvi c,':'! call conoutx
	lxi b,cnsmsg! call xprint
	pop h! push h! lxi b,console! dad b
	mov a,m! adi '0'! mov c,a! call conoutx
	lxi b,progmsg! call xprint
	pop h! call dsplynm

dskrst7:
	pop h ;remove return addr from diskreset
	lxi h,0ffffh! shld aret ;flag the error
	ret
;
deniedmsg:
	db cr,lf,'Disk reset denied, Drive ',0
cnsmsg:
	db ' Console ',0
progmsg:
	db ' Program ',0
;
;
ENDIF
;
;	individual function handlers
func12:
	;return version number
IF MPM
	lxi h,0100h+dvers! jmp sthlret
ELSE
	mvi a,dvers! jmp staret ;lret = dvers (high = 00)
ENDIF
;	ret ;jmp goback
;
func13:
IF MPM
	lhld dlog! shld info
	call diskreset! jz resetall
	call reset37
	jmp func13cont
resetall:
ENDIF
	;reset disk system - initialize to disk 0
	lxi h,0! shld rodsk! shld dlog
IF MPM
	shld rlog! shld tlog
func13cont:
ENDIF
	xra a! STA SELDSK ;note that usrcode remains unchanged
	DCR A! STA CURDSK 
IF MPM
	xra a! call getmemseg ;A = mem seg tbl index
	ora a! rz
	inr a! rz
	call rlradr! lxi b,msegtbl-rlros! dad b
	add a! add a! mov e,a! mvi d,0! dad d
	mov h,m! mvi l,80h
	jmp intrnlsetDMA
ELSE
	lxi h,tbuff! shld dmaad ;dmaad = tbuff
        JMP SETDATA ;to data dma address
ENDIF
	;ret ;jmp goback
;
func14:	
	;select disk info
	CALL SETSELDSK ; SELDSK = LINFO
IF MPM
	call curselect
	call rlr! lxi b,diskselect! dad b
	mov a,m! ani 0fh! rrc! rrc! rrc! rrc
	mov b,a! lda seldsk! ora b! rrc! rrc! rrc! rrc
	mov m,a! ret
ELSE
	JMP CURSELECT
ENDIF
	;ret ;jmp goback
;
func15:
	;open file
	call clrmodnum ;clear the module number
	call reselect
IF MPM
	xra a! sta makeflag
	call setsdcnt
	lxi h,openfile! push h
	mvi a,0c9h! sta checkfcb4
	call checkfcb1
	pop h! lda highext! cpi 060h! jnz openfile
	call home! call setenddir
	jmp openuserzero
openfile:
	lhld info! lda usrcode! mov m,a
	call setsdcnt
ENDIF
	CALL RESETCHKSUMFCB ; SET INVALID CHECK SUM
	CALL CHECKWILD ; CHECK FOR WILD CHARS IN FCB
	CALL GETATTS! ANI 11000000B ; A = ATTRIBUTES
IF MPM
 	cpi 11000000b! jnz attok
ENDIF
	ANI 01000000B ; MASK OFF UNLOCK MODE 
ATTOK:
	STA HIGHEXT
IF MPM
	mov b,a! ora a! rar! jnz attset
	mvi a,80h
attset:
	sta attributes! mov a,b
ENDIF
	ANI 80H! JNZ CALLOPEN
	LDA USRCODE! ORA A! JZ CALLOPEN 
	MVI A,0FEH! STA XDCNT+1! INR A! STA SEARCHUSER0
IF MPM
	sta sdcnt0+1
ENDIF
;
CALLOPEN:
	CALL OPEN! CALL OPENX ; RETURNS IF UNSUCCESSFUL, A = 0
	LXI H,SEARCHUSER0! CMP M! RZ
	MOV M,A! LDA XDCNT+1! CPI 0FEH! RZ 
;
;	FILE EXISTS UNDER USER 0
;
IF MPM
	call swap
ENDIF
	CALL SETDCNTDBLK
	MVI A,01100000B! STA HIGHEXT
OPENUSERZERO:
	; SET FCB USER # TO ZERO
	LHLD INFO! MVI M,0
	MVI C,NAMLEN! CALL SEARCHI! CALL SEARCHN
	CALL OPEN1 ; ATTEMPT REOPEN UNDER USER ZERO
	CALL OPENX ; OPENX RETURNS ONLY IF UNSUCCESSFUL
	RET
OPENX:
	CALL ENDOFDIR! RZ
	; OPEN SUCCESSFUL
	POP H ; DISCARD RETURN ADDRESS
	; WAS FILE OPENED UNDER USER 0 AFTER UNSUCCESSFUL
	; ATTEMPT TO OPEN UNDER USER N
IF MPM
	LDA HIGHEXT! CPI 060H! JZ OPENX00 ;YES
	; WAS FILE OPENED IN LOCKED MODE?
	ORA A! JNZ OPENX0 ; NO
	; DOES USER = ZERO?
	LHLD INFO! ORA M! JNZ OPENX0 ; NO
	; DOES FILE HAVE READ/ONLY ATTRIBUTE SET?
	CALL ROTEST! JNC OPENX0 ; NO
	; DOES FILE HAVE SYSTEM ATTRIBUTE SET?
	INX H! MOV A,M! RAL! JNC OPENX0 ; NO

	; FORCE OPEN MODE TO READ/ONLY MODE AND SET USER 0 FLAG
	; IF FILE OPENED IN LOCKED MODE, USER = 0, AND
	; FILE HAS READ/ONLY AND SYSTEM ATTRIBUTES SET

OPENX00:
ELSE
	LDA HIGH_EXT! CPI 060H! JNZ OPENX0 ; NO
ENDIF
	; IS FILE UNDER USER 0 A SYSTEM FILE ?
	MVI A,20H! STA ATTRIBUTES
	LHLD INFO! LXI D,10! DAD D
	MOV A,M! ANI 80H! JNZ OPENX0 ; YES - OPEN SUCCESSFUL
	; OPEN FAILS
	STA HIGHEXT! JMP LRETEQFF
OPENX0:
	CALL RESETCHKSUMFCB
	;
	;DOES DIR LBL EXIST?
IF MPM
	call getdirmode! ora a! jz openx1
ELSE
	CALL GETDIRMODE! ORA A! JZ SETFCBCKSFLAG
ENDIF
	;YES - IS FILE PASSWORD PROTECTED FOR OPEN?
	PUSH A! CALL GETXFCB! POP B
	ANI 0C0H! JZ OPENX1
	;YES - HAVE PASSWORDS BEEN ENABLES BY DIR LBL?
	MOV C,A! MOV A,B! ANI 80H! JZ OPENX1
	;YES - CHECK PASSWORD
	PUSH B! CALL CMPPW! POP B! JZ OPENX1
	MOV A,C! ANI 40H! CZ CHKPW1
	MVI A,080H! STA XFCBREADONLY
OPENX1:
;
IF MPM
	call packsdcnt
	;is this file currently open?
	mvi b,3! call searcholist! jz openx04
openx01:
	;no - is olist full?
	lhld freeroot! mov a,l! ora h! jnz openx03
	;yes - error
openx02:
	mvi a,11! jmp setaret
openx03:
	;has process exceeded open file maximum?
	call countopens! sub m! jc openx035
	;yes - error
openx034:
	mvi a,10! jmp setaret
openx035:
	;create new olist element
	call createolistitem
	jmp openx08
openx04:
	;do file attributes match?
	inx h! inx h
	lda attributes! ora m! cmp m! jnz openx06
	;yes - is open mode locked?
	ani 80h! jnz openx07
	;no - has this file been opened by this process?
	lhld prvpos! shld curpos
	mvi b,5! call searchnlist! jnz openx01
openx05:
	;yes - increment open file count
	lxi d,8! dad d! inr m! jnz openx08
	;count overflow
	inx h! inr m! jmp openx08
openx06:
	;error - file opened by another process in imcompatible mode
	mvi a,5! jmp setaret
openx07:
	;does this olist item belong to this process?
	dcx h! dcx h! push h
	call comparepds
	pop h! jnz openx06 ; no - error
	jmp openx05 ; yes
openx08:; open ok
	;was file opened in unlocked mode?
	lda attributes! ani 40h! jz openx09 ; no
	;yes - return .olist$item in ranrec field of fcb
	call getrra
	lxi d,curpos! mvi c,2! call move
openx09:
;
	call setfcbcksflag
	lda makeflag! ora a! rnz
	call getdirmode
ELSE
	CALL SETFCBCKSFLAG! MOV A,B
ENDIF
	;IS FILE ACCESS STAMPING ENABLED BY DIR LBL?
	ANI 40H! RZ
	CALL ENDOFDIR! RZ
	;YES - IS FILE READ/WRITE?
	CALL NOWRITE! RNZ ; CANNOT STAMP IF DISK READ ONLY
	;YES - UPDATE FCB ACCESS STAMP
OPENX2:
	CALL STAMP1
	JMP SEEKCOPY
;
func16:
	;close file
	call reselect
	CALL GETATTS! STA ATTRIBUTES
IF MPM
	lxi h,close00! push h
	mvi a,0c9h! sta checkfcb4
	call checkfcb1! pop h
	call setsdcnt
	call getmodnum! ani 80h! jnz close01
	call close! jmp close02
close00:
	mvi a,6! jmp setaret
close01:
	mvi a,0ffh! sta dontclose! call close1
close02:
ELSE
	CALL CHEKFCB! MVI A,6! JNZ SETARET
	CALL CLOSE
ENDIF
	LDA LRET! INR A! RZ
	CALL FUNC48 ; FLUSH BUFFERS
	LDA ATTRIBUTES! ANI 10000000B! RNZ
IF MPM
	call packsdcnt
	;find olist item for this process & file
	mvi b,5! call searcholist! jnz close03
	;decrement open count
	push h! lxi d,8! dad d
	mov a,m! sui 1! mov m,a! inx h
	mov a,m! sbi 0! mov m,a! dcx h
	;is open count = 0ffffh
	call testffff! pop h! jnz close03
	;yes - remove file's olist entry
	shld fileid! call deleteitem
	call resetchksumfcb
	;if unlocked file, remove file's locktbl entries
	call testunlocked! jz close03
	lhld fileid! call removelocks
close03:
ENDIF
	;
	;HAS FILE BEEN WRITTEN TO SINCE IT WAS OPENED?
	CALL GETMODNUM! ANI 40H! RZ
	;YES - IS FILE UPDATE STAMPING ENABLED BY DIR LBL?
	CALL GETDIRMODE! ANI 20H! RZ
	;YES - DOES XFCB EXIST FOR FILE?
	CALL GETXFCB! RZ
	;YES - UPDATE XFCB UPDATE STAMP
	CALL STAMP2
	JMP SEEKCOPY
	;ret ;jmp goback
;
func17:
	;search for first occurrence of a file
	xchg! XRA A
CSEARCH:
	PUSH A
	mov a,m! cpi '?'! JNZ CSEARCH1 ; NO RESELECT IF ?
	CALL CURSELECT! MVI C,0! JMP CSEARCH3
CSEARCH1:
	CALL GETEXTA! MOV A,M! CPI '?'! JZ CSEARCH2
	CALL CLREXT! CALL CLRMODNUM
CSEARCH2:
	CALL RESELECTX
	MVI C,NAMLEN
CSEARCH3:
	POP A
IF NOT MPM
	LXI H,DIRTOUSER
	PUSH H
ENDIF
	JNZ SEARCHN
	JMP SEARCH
	;ret ;jmp goback
;
func18:
	;search for next occurrence of a file name
IF MPM
	xchg! shld searcha
ELSE
	lhld searcha! shld info
ENDIF
;
	ORI 1! JMP CSEARCH
	;ret ;jmp goback
;
func19:
	;delete a file
	call reselectx
	jmp delete
	;ret ;jmp goback
;
func20:
	;read a file
	call reselect
	CALL CHECKFCB
	jmp seqdiskread				;
	 ;jmp goback
;
func21:
	;write a file
	call reselect
	CALL CHECKFCB
	jmp seqdiskwrite			;
	 ;jmp goback
;
func22:
	;make a file
	CALL GETATTS! STA ATTRIBUTES
	XRA A! STA FCBEXISTS
	CALL CLREXT
	call clrmodnum ; FCB MOD = 0
	call reselect
	CALL RESETCHKSUMFCB
	CALL CHECKWILD
	CALL SETXDCNT ; RESET XDCNT FOR MAKE
IF MPM
	call setsdcnt
ENDIF
	CALL OPEN ; VERIFY FILE DOES NOT ALREADY EXIST
	CALL RESETCHKSUMFCB
	CALL ENDOFDIR! JNZ FILEEXISTS ; 
IF MPM
	lda attributes! ani 80h! rrc! jnz makex00
	mvi a,80h
makex00:
	sta makeflag
	lda sdcnt+1! inr a! jz makex01
	call packsdcnt
	mvi b,3! call searcholist! jz makex02
makex01:
	lhld freeroot! mov a,l! ora h! jz openx02
	jmp makex03
makex02:
	inx h! inx h
	lda makeflag! ana m! jz openx06
	dcx h! dcx h! call comparepds! jz makex03
	lda makeflag! ral! jc openx06
makex03:
ENDIF
	;DO ANY FCBS FOR FILE EXIST IN THE DIRECTORY?
	LDA FCBEXISTS! ORA A! JZ MAKEX04
	;YES - DOES DIR LBL REQUIRE PASSWORDS?
	CALL GETDIRMODE! ANI 80H! JZ MAKEX04
	;YES - DOES XFCB EXIST WITH MODE 1 OR 2 PASSWORD?
	CALL GETXFCB! ANI 0C0H! JZ MAKEX04
	;YES - CHECK PASSWORD
	CALL CHKXFCBPASSWORD1
MAKEX04:
;
	CALL MAKE
	CALL RESETCHKSUMFCB
	CALL ENDOFDIR! RZ ; RETURN IF MAKE UNSUCCESSFUL
	;
	;DOES DIR LBL EXIST?
	CALL GETDIRMODE! ORA A! JZ MAKE4
	;YES - DOES XFCB ALREADY EXIST FOR FILE
	CALL GETXFCB! JNZ MAKE000
	;NO - DOES DIR LBL INVOKE AUTO XFCB CREATE
	CALL GETDIRMODE! ANI 10H! JZ MAKE4
	MVI A,0FFH! STA MAKEXFCB! CALL MAKE! JNZ MAKE00
	MVI C,NAMLEN! CALL SEARCH	
	CALL DELETE0! JMP LRETEQFF
MAKE000:
	;YES - IS THIS FIRST FCB FOR FILE?
	LDA FCBEXISTS! ORA A! JNZ MAKE4
MAKE00:
	CALL INITXFCB 
MAKE1:
	CALL SETFCBCKSFLAG
	;DID USER SET PASSWORD ATTRIBUTE
	LDA ATTRIBUTES! ANI 40H! JZ MAKE3
	XCHG! LHLD XDMAAD! LXI B,8! DAD B! XCHG
	LDAX D! ANI 0E0H! JNZ MAKE2
	MVI A,80H
MAKE2:
	PUSH A! CALL GETXFCB1! POP A! MOV M,A
	CALL SETPW! MOV M,B
MAKE3:
	CALL OPENX2
MAKE4:
IF MPM
	lda makeflag! sta attributes
	ani 40h! ral! sta highext
	lda sdcnt+1! inr a! jnz makexx02
	call sdcnteqxdcnt! call packsdcnt
	jmp openx03
makexx02:
	call fixolistitem! jmp openx1
ENDIF
	JMP SETFCBCKSFLAG
	;ret ;jmp goback
FILEEXISTS:
	MVI A,8! 
SETARET:
	MOV C,A! STA ARET+1! CALL LRETEQFF
IF MPM
	call testerrormode! jnz goback
ELSE
	LDA ERRORMODE! INR A! RZ
	MVI A,40H! ADD C! STA XERRID
ENDIF
IF MPM
	mov a,c! sui 3
	mov l,a! mvi h,0! dad h
	lxi d,xerrlist! dad d
	mov e,m! inx h! mov d,m
	xchg! jmp reporterr
ELSE
	LXI H,XERROR! JMP REPORTERR
ENDIF
;
func23:
	;rename a file
	call reselectx
	jmp rename
	;ret ;jmp goback
;
func24:
	;return the login vector
	lhld dlog! jmp sthlret			;
;	ret ;jmp goback
;
func25:
	;return selected disk number
	LDA SELDSK! jmp staret			;
;	ret ;jmp goback
;
func26:
IF MPM
	;save DMA address in process descriptor
	lhld info
intrnlsetDMA:
	xchg
	call rlr! lxi b,disksetDMA! dad b
	mov m,e! inx h! mov m,d
ENDIF
	;set the subsequent dma address to info
	xchg! shld dmaad ;dmaad = info
        jmp setdata ;to data dma address
;
func27:
	;return the login vector address
	CALL XCURSELECT
	lhld alloca! jmp sthlret
;	ret ;jmp goback
;
IF MPM
func28:
	;write protect current disk
	;first check for open files on disk
	mvi a,0ffh! sta setroflag
	lda seldsk! mov c,a! lxi h,0001h
	call hlrotl! call intrnldiskreset
	jmp setro
ELSE
func28:	equ	setro				;
	;write protect current disk
	;ret ;jmp goback
ENDIF
;
func29:
	;return r/o bit vector
	lhld rodsk! jmp sthlret		;
;	ret ;jmp goback
;
func30:
	;set file indicators
	CALL CHECKWRITE
	CALL CHECKWILD
	call reselectx
	CALL CHKPASSWORD
	call indicators
	jmp copydirloc ;lret=dirloc
	;ret ;jmp goback
;
func31:
	;return address of disk parameter block
	CALL XCURSELECT
	lhld dpbaddr
sthlret:
 	shld aret
	ret ;jmp goback
;
func32:
	;set user code
        lda linfo! cpi 0ffh! jnz setusrcode
		;interrogate user code instead
		lda usrcode! jmp staret ;lret=usrcode	
;		ret ;jmp goback
	setusrcode:
		ANI 0FH! sta usrcode
IF MPM
		push a
		call rlr! lxi b,diskselect! dad b
		pop b
		mov a,m! ani 0f0h! ora b! mov m,a
ENDIF
		ret ;jmp goback
;
func33:
	;random disk read operation
	call reselect
	CALL CHECKFCB
	jmp randiskread ;to perform the disk read
	;ret ;jmp goback
;
func34:
	;random disk write operation
	call reselect
	CALL CHECKFCB
	jmp randiskwrite ;to perform the disk write
	;ret ;jmp goback
;
func35:
	;return file size (0-65536)
	call reselect
	CALL CHECKWILD
	jmp getfilesize
	;ret ;jmp goback
;
func36:	equ setrandom
	;set random record
	;ret ;jmp goback
func37:
;
IF MPM
	call diskreset
reset37:
	lhld info
ELSE
	XCHG
ENDIF
	mov a,l! cma! mov e,a! mov a,h! cma
	lhld dlog! ana h! mov d,a! mov a,l! ana e
	mov e,a! lhld rodsk! xchg! shld dlog
IF MPM
	push h! call hleqhlandde
ELSE
	mov a,l! ana e! mov l,a
	mov a,h! ana d! mov h,a
ENDIF
	shld rodsk! 
;
IF MPM
	pop h! xchg! lhld rlog! call hleqhlandde! shld rlog
ENDIF
	LHLD DLOG! LDA CURDSK! CALL TESTVECTOR1
	RNZ ; RETURN IF CURDSK NOT RESET
	MVI A,0FFH! STA CURDSK
	; FORCE SELECT CALL IN NEXT CURSELECT
	RET
IF MPM
;
func38:				; access drive
	lxi h,packeddcnt! mvi a,0ffh
	mov m,a! inx h! mov m,a! inx h! mov m,a
	xra a! xchg! lxi b,16
accdrv0:
	dad h! adc b! dcr c! jnz accdrv0
	ora a! rz
	sta multcnt! dcr a! push a
	call accdrv02
	pop a! jmp openx02 ; insufficient free lock list items
accdrv02:
	call checkfree! pop h ; discard return addr, free space exists
	call countopens! pop b! add b! jc openx034
	sub m! jnc openx034 ; openmax exceeded
	lhld info! lda curdsk! push a! mvi a,16
accdrv1:
	dcr a! dad h! jc accdrv2
accdrv15:
	ora a! jnz accdrv1
	pop a! sta curdsk! ret
accdrv2:
	push a! push h! sta curdsk
	call createolistitem
	pop h! pop a! jmp accdrv15
;
func39:				; free drive
	lhld openroot! mov a,h! ora l! rz
	xra a! sta incrpdcnt! inr a! sta freemode
	lhld info! mov a,h! cmp l! jnz freedrv1
	inr a! jnz freedrv1
	sta freemode! call freefiles! jmp freedrv3
freedrv1:
	lda curdsk! push a! mvi a,16
freedrv2:
	dcr a! dad h! jc freedrv4
freedrv25:
	ora a! jnz freedrv2
	pop a! sta curdsk
freedrv3:
	lda incrpdcnt! ora a! rz
	lda pdcnt! jmp chkolist1
freedrv4:
	push a! push h! sta curdsk
	call freefiles
	pop h! pop a! jmp freedrv25
ELSE
func38 	equ	funcret
func39	equ	funcret
ENDIF
;
func40:
	;random disk write with zero fill of unallocated block
	call reselect
	CALL CHECKFCB
	mvi a,2! sta seqio
	mvi	c,false
	call	rseek1
	cz	diskwrite	;if seek successful
	ret
;
func41	equ	funcret	;test & write
IF MPM
func42:				;record lock
	mvi a,0ffh! sta lockunlock! jmp lock
func43:				;record unlock
	xra a! sta lockunlock! jmp lock
ELSE
func42	equ	funcret	;record lock
func43	equ	funcret	;record unlock
ENDIF
;
func44:				;set multi-sector count
	mov a,e! ora a! jz lreteqff
	cpi 17! jnc lreteqff
	sta multcnt
IF MPM
	mov d,a
	call rlr! lxi b,multcntoff! dad b
	mov m,d
ENDIF
	ret
;
func45:				;set BDOS error mode
IF MPM
	call rlr! lxi b,pname+4! dad b
	call setpflag
	mov m,a! inx h
	call setpflag
	mov m,a! ret
;
setpflag:
	mov a,m! ani 7fh! inr e! rnz
	ori 80h! ret
ELSE
	mov a,e! sta errormode
ENDIF
	ret
;
func46:				;get free space
	;perform temporary select of specified drive
	call tmpselect
	lhld alloca! xchg ; DE = alloc vector addr
	call getnalbs ; get # alloc blocks
	; HL = # of allocation vector bytes
	; count # of true bits in allocation vector
	lxi b,0 ; BC = true bit accumulator
gsp1:	ldax d
gsp2:	ora a! jz gsp4
gsp3:	rar! jnc gsp3
	inx b! jmp gsp2
gsp4:	inx d! dcx h
	mov a,l! ora h! jnz gsp1
	;HL = 0 when allocation vector processed
	;compute maxall + 1 - BC
	lhld maxall! inx h
	mov a,l! sub c! mov l,a
	mov a,h! sbb b! mov h,a
	;HL = # of available blocks on drive
	lda blkshf! mov c,a! xra a
	call shl3bv
	;AHL = # of available sectors on drive
	;store AHL in beginning of current DMA
	xchg! lhld xdmaad! mov m,e! inx h
	mov m,d! inx h! mov m,a
	ret
;
IF MPM
func47	equ	funcret
ELSE
func47: 			;chain to program
	;save user# and seldsk in location 0004h
	lda usrcode
	add a! add a! add a! add a! mov b,a
	lda seldsk! ora b! sta base+0004h
	mvi a,0ffh! sta chainflg! jmp REBOOTX
ENDIF
;
func48:				;flush buffers
	lxi h,diocomp! push h
	lxi b,0ffffh! call setdmaf
	pop h! jmp setdata
;
IF MPM
func49	equ	funcret
ELSE
func49:				;ccp chain query
	lxi h,chainflg! mov a,m! ora a! rz
	mvi m,0! jmp lreteqff
ENDIF
;
IF MPM
func50	equ	funcret
ELSE
func50:				;direct bios call
	;DE -> function (1 byte)
	;      BC value (2 bytes)
	;      DE value (2 bytes)
	xchg
	mov a,m! inx h
	mov c,m! inx h! mov b,m! inx h
	mov e,m! inx h! mov d,m
	mov l,a! add a! add l

	lxi h,bios

	add l! mov l,a!
	mvi a,0! adc h! mov h,a! pchl
ENDIF
;
func100:			;set directory label
	;DE -> .fcb
	;      drive location
	;      name & type fields user's discretion
	;      extent field definition
	;      bit 1 (80h): enable passwords on drive
	;      bit 2 (40h): enable file access 	
	;      bit 3 (20h): enable file update stamping
	;      bit 8 (01h): assign new password to dir lbl
	call reselectx
	call checkwrite
	;does dir lbl exist on drive?
	lhld info! mvi m,20h! mvi c,1
	call setxdcnt! call search! jnz sdl1
	;no - make one
	mvi a,0ffh! sta makexfcb
	call make! rz ;no dir space
	call initxfcb! call stamp1 ; create date & time stamp
sdl1:
	;update date & time stamp
	call stamp2
	;verify password - new dir lbl falls through
	call getxfcb1! call chkxfcbpassword1
	lxi b,0! call initxfcb0
	;set dir lbl dta in extent field
	ldax d! ori 1h! mov m,a
	;low bit of dir lbl data set to indicate dir lbl exists
	;update drive's dir lbl vector element
	push h! lhld drvlbla! mov m,a! pop h
sdl2:
	;assign new password to dir lbl or xfcb?
	ldax d! ani 1! jz seekcopy
	;yes - new password field is in 2nd 8 bytes of dma
	lxi d,8! call adjustdmaad
	call setpw! mov m,b
	lxi d,-8! call adjustdmaad
	jmp seekcopy
;
func101:			;return directory label data
	;perform temporary select of specified drive
	call tmpselect
	call getdirmode! jmp staret
;
func102:			;read file xfcb
	call reselectx
	call checkwild
	;does xfcb exist for file?
	call getxfcb! mvi a,0ffh! jz staret
	;yes - copy xfcb to user's fcb
	lhld info! mvi c,nxtrec! jmp move ;ret
;
func103:			;write or update file xfcb
	call reselectx
	call checkwild
	;does xfcb exist for file?
	call setxdcnt
	call getxfcb! jnz wxfcb1
	;no - does dir lbl exist in directory?
	call getdirmode
	ora a! mvi a,0ffh! jz staret
	;yes - does file exist in directory?
	sta makexfcb
	mvi c,extnum! call search! rz
	;yes - attempt to make xfcb for file
	call make! rz ; no dir space
	;initialize xfcb
	call initxfcb 
wxfcb1:
	;verify password - new xfcb falls through
	call chkxfcbpassword
	;set xfcb options data
	push h! call getexta! pop d! xchg
	mov a,m! ora a! jnz wxfcb2
	ldax d! ani 1! jz seekcopy
wxfcb2:
	ldax d! ani 0e0h! jnz wxfcb3
	mvi a,80h
wxfcb3:
	mov m,a! jmp sdl2
;
func104:			;set current date and time
IF MPM
	call getstampadd
	call copystamp
	mvi m,0! ret
ELSE
	lxi h,stamp
	jmp copystamp
ENDIF
;
func105:			;get current date and time
IF MPM
	call getstampadd
ELSE
	lxi h,stamp
ENDIF
	xchg
copystamp:
	mvi c,4! jmp move ;ret
;
IF MPM
getstampadd:
	call rlradr! lxi b,-5! dad b
	ret
ENDIF
;
func106:			;set default password
IF MPM
	call getdfpwa! inr a! rz
	lxi b,7! dad b
ELSE
	LXI H,DFPASSWORD+7
ENDIF
	xchg! lxi b,8! push h
	jmp setpw0
;
func107:			;return serial number
IF MPM
	lhld sysdat! mvi l,181
ELSE
	lxi h,serial
ENDIF
	xchg! mvi c,6! jmp move
;
GOBACK0:
	XRA A! STA RETURNFFFF
	LXI H,0FFFFH! SHLD ARET
goback:
	;arrive here at end of processing to return to user
	lda resel! ora a! jz retmon
		;reselection may have taken place
		LDA COMPFCBCKS! ORA A! CNZ SETCHKSUMFCB
		LDA XFCBREADONLY! ORA A! JZ GOBACK05
		LHLD INFO! LXI D,7! DAD D
		ORA M! MOV M,A
	GOBACK05:
		CALL GETEXTA! LDA HIGHEXT! CPI 60H! JNZ GOBACK1
		LXI D,-4! DAD D! MVI A,80H
	GOBACK1:
		ORA M! MOV M,A
		LDA ACTUALRC! ORA A! JZ GOBACK2
		CALL GETRCNTA! ORA M! MOV M,A
	GOBACK2:
		lhld info! mvi m,0 ;fcb(0)=0
		lda fcbdsk! ora a! jz retmon
		;restore disk number
		mov m,a ;fcb(0)=fcbdsk
		lda olddsk! STA SELDSK
;
;	return from the disk monitor
retmon:
	lhld entsp! sphl
	lhld aret! mov a,l! mov b,h! ret
;
;
;	data areas
;
;	initialized data
efcb:	db	empty	;0e5=available dir entry
rodsk:	dw	0	;read only disk vector
dlog:	dw	0	;logged-in disks
IF MPM
rlog:	dw	0	;removeable logged-in disks
tlog:	dw	0	;removeable disk test login vector
ntlog:	dw	0	;new tlog vector
remdrv: db	0	;curdsk removable drive switch
			;0 = permanent drive, 1 = removable drive
ENDIF
IF NOT BNKBDOS
xdmaad	equ	$
ENDIF
dmaad:	dw	tbuff	;initial dma address
IF NOT MPM
buffa:	ds	word	;pointer to directory dma address
ENDIF
;
;	curtrka - alloca are set upon disk select
;	(data must be adjacent, do not insert variables)
;	(address of translate vector, not used)
cdrmaxa:ds	word	;pointer to cur dir max value
curtrka:ds	word	;current track address
curreca:ds	word	;current record address
DRVLBLA:DS	WORD	;CURRENT DRIVE LABEL BYTE ADDRESS
dpbaddr:ds	word	;current disk parameter block address
checka:	ds	word	;current checksum vector address
alloca:	ds	word	;current allocation vector address
addlist	equ	$-DPBADDR	;address list size
;
;	sectpt - offset obtained from disk parm block at dpbaddr
;	(data must be adjacent, do not insert variables)
sectpt:	ds	word	;sectors per track
blkshf:	ds	byte	;block shift factor
blkmsk:	ds	byte	;block mask
extmsk:	ds	byte	;extent mask
maxall:	ds	word	;maximum allocation number
dirmax:	ds	word	;largest directory number
dirblk:	ds	word	;reserved allocation bits for directory
chksiz:	ds	word	;size of checksum vector
offset:	ds	word	;offset tracks at beginning
dpblist	equ	$-sectpt	;size of area
;
;	local variables
RETURNFFFF:	DS	BYTE	;SEL ERR FLAG FOR FXS 27 & 31
tranv:	ds	word	;address of translate vector
lockunlock:
makeflag:
rmf:	ds	byte	;read mode flag for open$reel
incrpdcnt:
dirloc:	ds	byte	;directory flag in rename, etc.
fcbexists:
	ds	byte	;Make FCB exists flag
freemode:
seqio:	ds	byte	;1 if sequential i/o
linfo:	ds	byte	;low(info)
dminx:	ds	byte	;local for diskwrite
searchl:ds	byte	;search length
searcha:ds	word	;search address
ACTUALRC:
	DS	BYTE	;DIRECTORY EXT RECORD COUNT
SAVEXFCB:
	DS	BYTE	;SEARCH XFCB SAVE FLAG
single:	ds	byte	;set true if single byte allocation map
SELDSK: DS	BYTE	;CURRENTLY SELECTED DISK 
olddsk:	ds	byte	;disk on entry to bdos
rcount:	ds	byte	;record count in current fcb
extval:	ds	byte	;extent number and extmsk
SAVEMOD:
	DS	BYTE	;OPEN$REEL MODULE SAVE FIELD
SAVEEXT:
	DS	BYTE	;OPEN$REEL EXTENT SAVE FIELD
vrecord:ds	BYTE	;current virtual record
arecord:ds	word	;current actual record
	DS	BYTE
;			SHELL FCB SAVE AREA
SAVERANR:	DB	0,0,0	;RANDOM RECORD SAVE AREA
arecord1:	ds	word	;current actual block# * blkmsk
IF NOT MPM
MULTCNT:	DB	1	;MULTI-SECTOR COUNT
ERRORMODE 	DB	0	;BDOS ERROR MODE
ENDIF
ATTRIBUTES:	DB	0	;MAKE ATTRIBUTE HOLD AREA
IF NOT MPM
CHAINFLG:	DB	0	;CHAIN FLAG
STAMP:		DB	0FFH,0FFH,0FFH,0FFH
ENDIF
;
;******** Following variable order critical *****************
IF MPM
multcnt:	db	1	;multi-sector count
pdcnt:		db	0	;process descriptor count
ENDIF
HIGHEXT:	DB	0	;FCB HIGH EXT BITS
XFCBREADONLY:	DB	0	;XFCB READ ONLY FLAG
CURDSK:		DB	0	;CURRENT DISK
IF MPM
;				MP/M VARIABLES	*
packeddcnt:	db	0,0,0			;
pdaddr:		dw	0			;
;************************************************************
curpos:	dw	0			;
prvpos:	dw	0			;
sdcnt:		dw	0   	 		;
sdblk:		dw	0   			;
sdcnt0:		dw	0			;
sdblk0:		dw	0			;
dontclose:	db	0			;
opencnt:			;MP/M temp variaable for open
lockcnt:	dw	0	;MP/M temp variable for lock
fileid:	dw	0	;MP/M temp variable for lock
deletedfiles:	db	0
lockshell:	db	0
locksp:	dw	0
setroflag:	db	0
checkdisk:	db	0
flushed:	db	0
fcbcksvalid:  db	0
;				MP/M VARIABLES  *
ENDIF
;
;	local variables for directory access
dptr:	ds	byte	;directory pointer 0,1,2,3
XDCNT:  DS	WORD	;EMPTY DIRECTORY DCNT
XDBLK:  DS	WORD	;EMPTY DIRECTORY BLOCK
dcnt:	ds	word	;directory counter 0,1,...,dirmax
DBLK:	DS	WORD	;DIRECTORY BLOCK INDEX
USER0PASS:
	DS	BYTE	;SEARCH USER 0 PASS FLAG
;
;	local variables initialized by bdos at entry
;
resel:		ds	byte	;reselection flag
fcbdsk:		ds	byte	;disk named in fcb
COMPFCBCKS:	DS	BYTE	;COMPUTE FCB CHECKSUM FLAG
SEARCHUSER0:	DS	BYTE	;SEARCH USER 0 FOR FILE (OPEN)
MAKEXFCB:	DB	BYTE	;MAKE & SEARCH XFCB FLAG
FINDXFCB:	DB	BYTE	;SEARCH FIND XFCB FLAG
;
DFPASSWORD:	DW	0,0,0,0
IF MPM
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
		dw	0,0,0,0
ENDIF
;
IF MPM
	ds	192
last:
	org	(((last-base)+255) AND 0ff00h) - 192

	;	BNKBDOS patch area

	dw	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dw	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dw	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dw	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dw	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
	dw	0,0,0,0,0,0,0,0,0,0,0,0

freeroot:	dw	$-$			;
openroot:	dw	0			;
lockroot:	dw	0			;
lockmax:	db	0			;
openmax:	db	0			;
;
;	bios access table
bios	equ	$		;base of the bios jump table
bootf	equ	bios		;cold boot function
wbootf	equ	bootf+3		;warm boot function
constf	equ	wbootf+3	;console status function
coninf	equ	constf+3	;console input function
conoutf	equ	coninf+3	;console output function
listf	equ	conoutf+3	;list output function
punchf	equ	listf+3		;punch output function
readerf	equ	punchf+3	;reader input function
homef	equ	readerf+3	;disk home function
seldskf	equ	homef+3		;select disk function
settrkf	equ	seldskf+3	;set track function
setsecf	equ	settrkf+3	;set sector function
setdmaf	equ	setsecf+3	;set dma function
readf	equ	setdmaf+3	;read disk function
writef	equ	readf+3		;write disk function
liststf	equ	writef+3	;list status function
sectran	equ	liststf+3	;sector translate
;
ENDIF
	end
