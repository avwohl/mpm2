; bnkxios_stub.asm - Minimal BNKXIOS for MP/M II emulator
; This stub forwards all calls to the emulator XIOS at FC00H
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later

    .z80
    org 0

; XIOS entries must be exactly 3 bytes each
; All entries jump to the emulator XIOS at FC00H

; Emulator XIOS base address - must not conflict with MP/M system areas
; Must be below 9100H (TMP) but in common memory (8000+)
; Using 8800H which is in common area below all SPR loads
XIOS_BASE   equ 08800h

; Offset 0000: COLDBOOT
coldboot:
    jp  XIOS_BASE+00h       ; 3 bytes

; Offset 0003: WBOOT
warmstart:
    jp  XIOS_BASE+03h

; Offset 0006: CONST
const:
    jp  XIOS_BASE+06h

; Offset 0009: CONIN
conin:
    jp  XIOS_BASE+09h

; Offset 000C: CONOUT
conout:
    jp  XIOS_BASE+0Ch

; Offset 000F: LIST
list:
    jp  XIOS_BASE+0Fh

; Offset 0012: PUNCH
punch:
    jp  XIOS_BASE+12h

; Offset 0015: READER
reader:
    jp  XIOS_BASE+15h

; Offset 0018: HOME
home:
    jp  XIOS_BASE+18h

; Offset 001B: SELDSK
seldsk:
    jp  XIOS_BASE+1Bh

; Offset 001E: SETTRK
settrk:
    jp  XIOS_BASE+1Eh

; Offset 0021: SETSEC
setsec:
    jp  XIOS_BASE+21h

; Offset 0024: SETDMA
setdma:
    jp  XIOS_BASE+24h

; Offset 0027: READ
read:
    jp  XIOS_BASE+27h

; Offset 002A: WRITE
write:
    jp  XIOS_BASE+2Ah

; Offset 002D: LISTST
listst:
    jp  XIOS_BASE+2Dh

; Offset 0030: SECTRAN
sectran:
    jp  XIOS_BASE+30h

; Offset 0033: SELMEMORY (MP/M)
selmemory:
    jp  XIOS_BASE+33h

; Offset 0036: POLLDEVICE (MP/M)
polldevice:
    jp  XIOS_BASE+36h

; Offset 0039: STARTCLOCK (MP/M)
startclock:
    jp  XIOS_BASE+39h

; Offset 003C: STOPCLOCK (MP/M)
stopclock:
    jp  XIOS_BASE+3Ch

; Offset 003F: EXITREGION (MP/M)
exitregion:
    jp  XIOS_BASE+3Fh

; Offset 0042: MAXCONSOLE (MP/M)
maxconsole:
    jp  XIOS_BASE+42h

; Offset 0045: SYSTEMINIT (MP/M)
systeminit:
    jp  XIOS_BASE+45h

; Offset 0048: IDLE (MP/M)
idle:
    jp  XIOS_BASE+48h

; Commonbase structure at offset 0x4B (3 bytes after IDLE at 0x48)
; These entries are used by XDOS/BNKBDOS for bank switching and dispatch
; The emulator intercepts at XIOS_BASE+4B+ and handles them

; Offset 004B: COMMONBASE
commonbase:
    jp  XIOS_BASE+4Bh

; Offset 004E: SWTUSER - switch to user bank (first entry of commonbase structure)
swtuser:
    jp  XIOS_BASE+4Eh

; Offset 0051: SWTSYS - switch to system bank
swtsys:
    jp  XIOS_BASE+51h

; Offset 0054: PDISP - process dispatcher
pdisp:
    jp  XIOS_BASE+54h

; Offset 0057: XDOSENT - XDOS entry point
xdosent:
    jp  XIOS_BASE+57h

; Offset 005A: SYSDAT - system data pointer (2-byte pointer, not callable)
; This is DATA, not code. XDOS reads this pointer to find SYSDAT at FF00H.
sysdat:
    dw  0FF00h                  ; 2 bytes: pointer to system data at FF00H

; End of XIOS stub - pad to 256 bytes
    ds  256 - ($-coldboot), 0
