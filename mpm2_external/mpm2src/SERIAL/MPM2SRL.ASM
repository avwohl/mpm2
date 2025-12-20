;	title	'Serialization Program for MP/M II  09/14/81'
version	equ	20	;version 2.0
system	equ	1	;MP/M II system

;	Serialization Program for MP/M II diskettes

; Note **********************************************************
; This program contains Digital Research proprietary information,
; and must not be reproduced, copied, or transcribed in any form
; whatsoever
; ***************************************************************


;	Copyright (C) 1979, 1980, 1981
;	Digital Research
;	Box 579 Pacific Grove
;	California, 93950

;	Revised:
;	  14 Sept 81  by Thomas Rolander

tpa	equ	100h	;transient program area
	org	tpa
	jmp	start
Copyright:
	db	'COPYRIGHT (C) 1981,'
CoNm:
	db	' DIGITAL RESEARCH '
CoNmLen	equ	$-CoNm
;
;	global equates
boot	equ	0000h
bdos	equ	0005h
;
;	BDOS equates
conin	equ	1	;read console device
conout	equ	2	;write console device
pbuff	equ	9	;print buffer
rdbuff	equ	10	;read buffer
resetds	equ	13	;reset disk system
selectd	equ	14	;select disk
openf	equ	15	;open file
rdseqf	equ	20	;read sequential file
wrseqf	equ	21	;write sequential file
stdmadr	equ	26	;set DMA address

;	Hardware dependent equates
begtrk	equ	0	;beginning track #
dirtrk	equ	2	;directory track #
maxtrk	equ	76	;maximum track #
sectrk	equ	26	;sectors per track
srcdsk	equ	2	;source disk
dstdsk	equ	3	;destination disk
secsiz	equ	128	;sector size in # bytes
nsrlsec	equ	4	;# of sectors to search serial #

cr	equ	0dh
lf	equ	0ah
;
readtrk	equ	$
writtrk	equ	$+3
reread	equ	$+6
;	track read/write routines, filled in later
	jmp	read$disk
	jmp	write$disk
	jmp	re$read$disk
track:	ds	1	;set to track to read/write
buffa:	ds	2	;set to buffer address to read/write
iof:	ds	1	;io function 0 = read, 1 = write
;
read$disk:
	;read disk drive a, from track 'track'
	;into the address given by 'buffa'
	mvi a,srcdsk! call sel ;select src drive
	jmp read$d
;
;
write$disk:
	;write to disk drive b, to track 'track'
	;from the address given by 'buffa'
	mvi a,1! sta iof ;set iofunction to write
	mvi a,dstdsk! call sel ;select dst drive
	jmp rw$disk
;
re$read$disk:
	;read from dstdsk, from track 'track'
	;to the address given by 'buffa'
	mvi a,dstdsk! call sel ;dst drive selected
	read$d: xra a! sta iof ;set to read function
	rw$disk: ;read or write disk
		lxi h,track! mov c,m ;get track number
		call trk ;track selected
		lhld buffa ;get dma address
		lxi d,sectrk ;d = 0, e = sectors per track
		rw$loop: ;read/write loop
			inr d ;to next sector
			push d! push h! mov c,d! mvi b,0
			lxi h,skewtbl! mov a,m! ora a
			jz noskew ;jump if no skew table
			dcr c! dad b! mov c,m
		    noskew:
			call sec ;sector set
			pop b! push b ;get dma address
			call dma ;dma address set
			;perform io function
			lda iof! ora a ;0=read, 1=write
			jnz writefunc
				;read disk
				call dread
				jmp rwcomplete
			writefunc:
				;write disk
				call dwrite
			rwcomplete: ;function complete
				pop h ;recall dma address
				lxi d,secsiz! dad d ;to nxt dma
				pop d ;recall sector and count
			; check error conditions
			ora a! rnz ;rtn with non zero flag set
			dcr e ;count = count - 1
			jnz rw$loop ;for another sector
			ret ;with zero flag set for io complete
;
;
;	utility subroutines for direct disk io
wboot	equ	1	;warm boot address
seldsk	equ	24	;select disk
settrk	equ	27	;set track
setsec	equ	30	;set sector
setdma	equ	33	;set dma address
readf	equ	36	;read disk
writf	equ	39	;write disk
;
sel:	;select drive given by register a
	mov c,a! lhld wboot! lxi d,seldsk! dad d! pchl
;
trk:	;set track given by c
	lhld wboot! lxi d,settrk! dad d! pchl
;
sec:	;set sector given by c
	lhld wboot! lxi d,setsec! dad d! pchl
;
dma:	;set dma address to value of b,c
	lhld wboot! lxi d,setdma! dad d! pchl
;
dread:	;perform read operation
	lhld wboot! lxi d,readf! dad d! pchl
;
dwrite:	;perform write operation
	lhld wboot! lxi d,writf! dad d! pchl
;
start:
	lxi sp,stack
	call signonmsg ;display sign on message
	call sysdskmsg! call ci ;system disk serialize ?
	ani 0dfh! cpi 'Y'! mvi a,dirtrk
	jnz sd1! lxi h,sysdsk! inr m! mvi a,begtrk
	sd1: sta strtrk
	call dskslctmsg! call read ;select 1/2, 2/2, or 1/2 & 2/2
	lxi h,disk! mov m,c
	call orgmsg! call read ;origin prompt
	lxi h,origin! mov m,c ;saved the origin number
	inx h! mov m,b ;high order origin number byte
	lxi h,comlen! mov b,m! inx h! xchg
	lxi h,aorigin ;ascii version of the origin
	org2: ldax d! ora a! jz org3
		inx d! mov m,a! inx h! dcr b! jnz org2
		;ascii version moved to buffer, pad it
	org3:	mvi m,'-'! inx h! mvi m,'$' ;ready for printing
;
;	now read the serial number
	call sermsg! call read
	lxi h,bserial! mov m,c! inx h! mov m,b ;binary copied
	lxi h,comlen! mvi a,5! sub m ;difference in reg-a
	lxi h,aserial! jz pad1 ;pad high order positions with 0
	pad0:	mvi m,'0'! inx h! dcr a! jnz pad0
	pad1:	lxi d,cbuff ;addressing buffer
	pad2:	ldax d! ora a! jz pad3 ;looking for binary 0
	mov m,a! inx h! inx d! jmp pad2 ;another char
	pad3:	;end of ascii fill (right adjusted in aserial)
;
rddsk:	;read disk and copy
	lxi sp,stack ;reset stack pointer (entry from errors)
	call insmsg! call ci ;wait for response
;
nextdisk:
	call curmsg! call asermsg! call newmsg! call ci
	;new disk is ready, try the copy operation
	lda strtrk ;get start track
	lxi h,track! mov m,a ;initialize the track number
	lxi h,trcount! mvi m,'0'! inx h! mvi m,'0'
;
rdtrk:	;read the next source track, compare with 0e5h for end
	lxi h,ibuff! shld buffa! call readtrk
		jz readok! call read0msg! jmp rddsk
	readok:
	;track is in memory, track 0?
	lda track! ora a
		;if track 0, do MPMLDR serialization
		cz serialize
	;
		;track in memory, check for last track
		lxi h,ibuff! lxi b,trlen
		trcomp:	mvi a,0e5h! cmp m! jnz wrtrk
			inx h! dcx b! mov a,c! ora b
			jnz trcomp
		;end of copy, all 0e5h's
	    endcopy:
		;write serial # into files in FCB table
		lda sysdsk! ora a! lxi h,FCBtable$disk1
		jz nonsysdsk! lxi h,FCBtable$sys$disk1
		nonsysdsk:
		;write serial # into disk1
		lda disk! ani 01h! cnz srlfile
		;write serial # into disk2
		lxi h,FCBtable$disk2
		lda disk! ani 02h! cnz srlfile
		lxi d,trmsg! call prmsg
		call incserial! jmp nextdisk
	;
	;not end of copy, write track to disk from ibuff
	wrtrk:	call writtrk
		jz wrok! call write0msg! jmp nextdisk
	wrok:
		;written to disk, now read it back and compare
		lxi h,obuff! shld buffa! call reread
			jz read1ok! call read1msg! jmp nextdisk
		read1ok:
		lxi h,ibuff! lxi d,obuff! lxi b,trlen
		wrcomp: ldax d! cmp m! jnz wrerr
			inx h! inx d! dcx b! mov a,c! ora b
			jnz wrcomp
		;compare ok, increment track count and cycle
		lxi h,track! inr m
		mvi a,maxtrk+1! cmp m
		;jump if last track on disk copied
		jz endcopy
		lxi h,trcount+1! inr m! mov a,m! cpi '9'+1
		jc rdtrk ;overflow to high order track number
		mvi m,'0'! dcx h! inr m! jmp rdtrk
		;
	wrerr:	;verify error
		call vererr! jmp nextdisk
;
;
;	utility subroutines
ci:	mvi c,conin! jmp bdos ;read a character
;
prmsg:	mvi c,pbuff! jmp bdos ;print a buffer
;
rderr:	call invalid
;
read:	;read constant value to b,c
	lxi d,maxlen! mvi c,rdbuff! call bdos ;buffer filled
	lxi h,comlen! mov a,m! ora a! jz rderr ;non zero length
	inx h! mov e,a! mvi d,0! dad d ;h,l address last pos+1
	mvi m,0 ;cleared for end of scan
	lxi h,cbuff! lxi b,0
	conv:	mov a,m! ora a! rz ;return if end of convert
		sui '0'! cpi 10! jnc rderr
		push h! push b! pop h ;b,c copied to h,l
		dad h! dad h! dad h! dad b! dad b
		push h! pop b! pop h ;bc=bc*10
		inx h! add c! mov c,a! mvi a,0! adc b! mov b,a
		jc rderr! jmp conv ;tested for overflow
	;
incserial:
	;increment the serial number
	lhld bserial! inx h! shld bserial ;test for overflow
	mov a,l! ora h! jz serover
	;
	lxi h,aserial+4! mvi b,5 ;length of serial number
	inc0:	inr m! mov a,m! cpi '9'+1! rc ;return if carry
		mvi m,'0' ;clear the number
		dcx h! dcr b! jnz inc0
	;
	serover:
		;overflow in serial number
		call overmsg
		jmp boot
;
serialize:
	;match ' DIGITAL RESEARCH ' and update serial #
		lxi h,ibuff! lxi b,nsrlsec*secsiz
		search:	push h! push b
			lxi d,CoNm! mvi c,CoNmLen
		comp0:	ldax d! cmp m
			jnz nomatch
			inx h! inx d! dcr c! jnz comp0
		;match complete, we've found the serial number
		pop b! pop d ;clears stack
		xchg! lhld origin! xchg! mov m,e ;low byte origin
		inx h! mvi m,version ;version number in binary
		inx h! mvi m,system ;system number in binary
		inx h! mov m,d ;high byte origin number
		inx h ;get the serial number
		xchg! lhld bserial! xchg
		mov m,d! inx h! mov m,e
		;MPMLDR serial number is copied
		ret
		;
	nomatch: ;try for next match
		pop b! pop h! inx h! dcx b! mov a,c! ora b
		jnz search
		;not found
		call noserial! jmp rddsk
;
rdwrsrlfl:
	sta iof ;save rdseqf / wrseqf func code
	push d! push h
	lxi d,32! dad d! mvi m,0 ;set nr = 0
	pop h! pop d! mvi b,nsrlsec ;count = 4 sectors
	flrdwr:
		push b! push d! push h
		mvi c,stdmadr! call bdos
		pop d! push d
		lda iof! mov c,a! call bdos
		pop h! pop d! pop b
		inr a! jz srlflerr
		dcr b! rz
		push h! lxi h,secsiz! dad d
		xchg! pop h
		jmp flrdwr
    srlflerr:
	call badMPM! jmp rddsk

;
srlfile:
	;update serial # in all files in the FCB table
	;HL = FCBTable address
	shld FCBtable$adr
	;reset disk system
	push h!mvi c,resetds! call bdos! pop h
	srlflopn:
		mov a,m! ora a! jz srlflrdwrvr
		push h! xchg! mvi c,openf
		call bdos ;open next file from FCB Table
		pop h! inr a! jz srlflerr
		lxi d,33! dad d! jmp srlflopn
	srlflrdwrvr:
	lhld FCBtable$adr
	flrdwrvr:
		mov a,m! ora a! rz
		push h
		srlflrd:
			lxi d,ibuff ;dma buffer is ibuff
			mvi a,rdseqf! call rdwrsrlfl
		srlflwr:
			call serialize ;patch in serial #
			pop h! push h
			lxi d,ibuff ;dma buffer is ibuff
			mvi a,wrseqf! call rdwrsrlfl
		srlflvr:
			pop h! push h
			lxi d,obuff ;dma buffer is obuff
			mvi a,rdseqf! call rdwrsrlfl
	
		lxi h,ibuff! lxi d,obuff! lxi b,nsrlsec*secsiz
		flvr:
			ldax d! cmp m
			jnz srlflerr
			inx h! inx d! dcx b
			mov a,b! ora c
			jnz flvr
		pop h! lxi d,33! dad d
		jmp flrdwrvr
;
FCBtable$sys$disk1:
;
;	MPM.SYS
	db	dstdsk+1	;drive code
	db	'MPM     '	;file name
	db	'SYS'		;file type
	db	0		;extent
	ds	2
	db	0		;record count
	ds	16		;allocation map
	db	0		;next record

FCBtable$disk1:
;	XDOS.SPR
	db	dstdsk+1	;drive code
	db	'XDOS    '	;file name
	db	'SPR'		;file type
	db	0		;extent
	ds	2
	db	0		;record count
	ds	16		;allocation map
	db	0		;next record
;
;	RESBDOS.SPR
	db	dstdsk+1	;drive code
	db	'RESBDOS '	;file name
	db	'SPR'		;file type
	db	0		;extent
	ds	2
	db	0		;record count
	ds	16		;allocation map
	db	0		;next record
;
;	MPMLDR.COM
	db	dstdsk+1	;drive code
	db	'MPMLDR  '	;file name
	db	'COM'		;file type
	db	0		;extent
	ds	2
	db	0		;record count
	ds	16		;allocation map
	db	0		;next record

;	GENSYS.COM
	db	dstdsk+1	;drive code
	db	'GENSYS  '	;file name
	db	'COM'		;file type
	db	0		;extent
	ds	2
	db	0		;record count
	ds	16		;allocation map
	db	0		;next record
;
	db	0		;end of FCB Table
;

;
FCBtable$disk2:
;
;	RMAC.COM
	db	dstdsk+1	;drive code
	db	'RMAC    '	;file name
	db	'COM'		;file type
	db	0		;extent
	ds	2
	db	0		;record count
	ds	16		;allocation map
	db	0		;next record
;
;	LINK.COM
	db	dstdsk+1	;drive code
	db	'LINK    '	;file name
	db	'COM'		;file type
	db	0		;extent
	ds	2
	db	0		;record count
	ds	16		;allocation map
	db	0		;next record
;
;	LIB.COM
	db	dstdsk+1	;drive code
	db	'LIB     '	;file name
	db	'COM'		;file type
	db	0		;extent
	ds	2
	db	0		;record count
	ds	16		;allocation map
	db	0		;next record
;
	db	0		;end of FCB Table

;	print strings
insmsg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Insert control diskette in '
	db 'A'+srcdsk
	db ':, type return$'
;
signonmsg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'MP/M II V'
	db version/10+'0','.',version mod 10 +'0'
	db ' Serialization,',cr,lf
;
sysdskmsg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'System disk (Y/N)? $'
;
dskslctmsg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Select type of disk serialization:'
	db cr,lf,'  1 = Disk 1/2'
	db cr,lf,'  2 = Disk 2/2'
	db cr,lf,'  3 = Disk 1/2 & 2/2'
	db cr,lf,'?$'
;
orgmsg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Origin number? $'
;
sermsg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Starting serial number? $'
;
invalid:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Invalid number, try again $'
;
curmsg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Serializing disk $'
;
asermsg:
	lxi d,aorigin! call prmsg
	lxi d,aserial! jmp prmsg
;
newmsg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Insert new diskette in '
	db 'A'+dstdsk
	db ':, type return$'
;
noserial:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Non MP/M II diskette, cannot serialize$'
;
badMPM:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Bad MP/M II files on disk$'
;
vererr:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Verification error, bad disk$'
;
overmsg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Serial number overflow$'
read0msg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Error on source disk, replace$'
;
write0msg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Cannot write new disk, replace$'
;
read1msg:
	lxi d,$+6! jmp prmsg
	db cr,lf,'Cannot re-read new disk, replace$'
;
;
;	miscellaneous data areas
;
disk:	ds	1	;1=1/2, 2=2/2, 3=1/2 & 2/2
sysdsk:	db	0	;0ffh="system disk"
strtrk:	ds	1	;start track, begtrk or dirtrk
FCBtable$adr: ds 2	;FCB table address
origin:	ds	2	;binary origin
aorig:	db	cr,lf
aorigin:
	db	'00000-$'
trmsg:	db	cr,lf
trcount:
	db	'00 tracks verified$'
bserial:
	ds	2	;binary serial number
aserial:
	db	'00000$'

;	skew table  (first byte 0 if no skewing)
skewtbl:
	db	0	;no skew table for MDS-800

;	input buffer
maxlen:	db	7
comlen:	ds	1
cbuff:	ds	8
;
	ds	32	;stack
stack:
trlen	equ	sectrk*secsiz	;buffer size
ibuff:	ds	trlen
obuff:	ds	trlen
	end	tpa
