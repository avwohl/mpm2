; coldboot.asm - Cold Start Loader for hd1k format
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; This loader resides in sector 0 of the boot disk.
; It loads MPMLDR and LDRBIOS from the reserved tracks into memory,
; sets up page zero, and jumps to MPMLDR.
;
; Memory map after loading:
;   0x0000-0x00FF  Page zero (set up by this loader)
;   0x0100-0x16FF  MPMLDR.COM (~1.5KB)
;   0x1700-0x1AFF  LDRBIOS (~1KB)
;
; Boot area layout on disk (track 0):
;   Physical sector 0 (logical 0-3):    Cold start loader (512 bytes max)
;   Physical sectors 1-12 (logical 4-51):  MPMLDR.COM (~6KB)
;   Physical sectors 13-16 (logical 52-67): LDRBIOS (~2KB)
;
; NOTE: CP/M uses 128-byte logical sectors. Each 512-byte physical sector
; contains 4 logical sectors. DiskSystem::read() reads one 128-byte sector.
;
; Assemble with: um80 -o coldboot.rel coldboot.asm
; Link with: ul80 -n -o coldboot.bin coldboot.rel

        .Z80
        ORG     0000H           ; Loaded at 0x0000 by hardware/emulator

; =============================================================================
; Constants
; =============================================================================

MPMLDR_ADDR:    EQU     0100H   ; Where to load MPMLDR
MPMLDR_SEC:     EQU     4       ; First logical sector of MPMLDR (phys 1 = log 4)
MPMLDR_NSEC:    EQU     48      ; Number of 128-byte logical sectors (12 phys × 4)

LDRBIOS_ADDR:   EQU     1700H   ; Where to load LDRBIOS (overlaps unused end of MPMLDR)
LDRBIOS_SEC:    EQU     52      ; First logical sector of LDRBIOS (phys 13 × 4)
LDRBIOS_NSEC:   EQU     16      ; Number of 128-byte logical sectors (4 phys × 4)

DISPATCH_PORT:  EQU     0E0H    ; I/O port for emulator dispatch

; Dispatch function codes (same as LDRBIOS/BNKXIOS)
FUNC_CONOUT:    EQU     00CH
FUNC_SELDSK:    EQU     01BH
FUNC_SETTRK:    EQU     01EH
FUNC_SETSEC:    EQU     021H
FUNC_SETDMA:    EQU     024H
FUNC_READ:      EQU     027H

; =============================================================================
; Entry point - execution starts here
; =============================================================================

START:
        DI                      ; Disable interrupts
        LD      SP, 0100H       ; Set up stack below load area

        ; Print boot banner
        LD      HL, MSG_BOOT
        CALL    PRINT

        ; Select drive A:
        LD      C, 0            ; Drive A
        LD      A, FUNC_SELDSK
        OUT     (DISPATCH_PORT), A

        ; Set track 0
        LD      HL, 0
        LD      A, FUNC_SETTRK
        OUT     (DISPATCH_PORT), A

        ; Load MPMLDR (sectors 1-3 -> 0x0100)
        LD      HL, MSG_MPMLDR
        CALL    PRINT

        LD      DE, MPMLDR_ADDR ; DMA address
        LD      B, MPMLDR_NSEC  ; Number of sectors
        LD      C, MPMLDR_SEC   ; Starting sector
        CALL    LOAD_SECTORS

        ; Load LDRBIOS (sectors 4-5 -> 0x1700)
        LD      HL, MSG_LDRBIOS
        CALL    PRINT

        LD      DE, LDRBIOS_ADDR
        LD      B, LDRBIOS_NSEC
        LD      C, LDRBIOS_SEC
        CALL    LOAD_SECTORS

        ; Set up page zero
        CALL    SETUP_PAGE0

        ; Print ready message
        LD      HL, MSG_READY
        CALL    PRINT

        ; Jump to MPMLDR
        JP      MPMLDR_ADDR

; =============================================================================
; LOAD_SECTORS - Load multiple 128-byte logical sectors
; Input:  DE = destination address
;         B  = number of 128-byte sectors to load
;         C  = starting logical sector number
; Uses:   A, HL
; =============================================================================

LOAD_SECTORS:
        PUSH    BC
        PUSH    DE

LS_LOOP:
        ; Set sector (C may overflow 8 bits, use HL for sector number)
        LD      H, 0
        LD      L, C            ; Sector in HL
        LD      A, FUNC_SETSEC
        OUT     (DISPATCH_PORT), A

        ; Set DMA address
        LD      H, D
        LD      L, E            ; DMA address in HL
        LD      A, FUNC_SETDMA
        OUT     (DISPATCH_PORT), A

        ; Read sector (128 bytes)
        LD      A, FUNC_READ
        OUT     (DISPATCH_PORT), A

        ; Check for error (A=0 success, A=1 error)
        OR      A
        JR      NZ, LS_ERROR

        ; Advance to next sector
        INC     C               ; Next logical sector

        ; Advance DMA by 128 bytes (0x80)
        LD      A, E
        ADD     A, 80H          ; Add 128: low byte += 0x80
        LD      E, A
        LD      A, D
        ADC     A, 0            ; high byte += carry only
        LD      D, A

        ; Loop
        DJNZ    LS_LOOP

        POP     DE
        POP     BC
        RET

LS_ERROR:
        LD      HL, MSG_ERROR
        CALL    PRINT
        DI
        HALT

; =============================================================================
; SETUP_PAGE0 - Initialize page zero vectors
; =============================================================================

SETUP_PAGE0:
        ; 0x0000: JP WBOOT (LDRBIOS + 3)
        LD      A, 0C3H         ; JP opcode
        LD      (0000H), A
        LD      HL, LDRBIOS_ADDR + 3
        LD      (0001H), HL

        ; 0x0003: IOBYTE
        XOR     A
        LD      (0003H), A

        ; 0x0004: Current disk/user
        LD      (0004H), A

        ; 0x0005: JP BDOS (MPMLDR has LDRBDOS at 0x032E)
        LD      A, 0C3H
        LD      (0005H), A
        LD      HL, 032EH       ; MPMLDR's internal LDRBDOS
        LD      (0006H), HL

        ; 0x0038: RST 38H - JP to tick handler (will be set up later by MPM.SYS)
        ; For now, just RET
        LD      A, 0C9H         ; RET opcode
        LD      (0038H), A

        ; Set up BIOS jump table at 0xC300 -> LDRBIOS at 0x1700
        ; MPMLDR expects BIOS calls at high memory
        LD      HL, 0C300H      ; Destination
        LD      DE, LDRBIOS_ADDR ; LDRBIOS base
        LD      B, 17           ; 17 BIOS entry points

SP_BIOS_LOOP:
        LD      A, 0C3H         ; JP opcode
        LD      (HL), A
        INC     HL
        LD      A, E
        LD      (HL), A
        INC     HL
        LD      A, D
        LD      (HL), A
        INC     HL

        ; Advance DE by 3 (next LDRBIOS entry)
        LD      A, E
        ADD     A, 3
        LD      E, A
        JR      NC, SP_NO_CARRY
        INC     D
SP_NO_CARRY:
        DJNZ    SP_BIOS_LOOP

        RET

; =============================================================================
; PRINT - Print null-terminated string
; Input: HL = string address
; =============================================================================

PRINT:
        LD      A, (HL)
        OR      A
        RET     Z

        LD      C, A            ; Character to output
        LD      D, 0            ; Console 0
        PUSH    HL
        LD      A, FUNC_CONOUT
        OUT     (DISPATCH_PORT), A
        POP     HL
        INC     HL
        JR      PRINT

; =============================================================================
; Messages - must fit before 0x0100 (where MPMLDR loads)
; =============================================================================

MSG_BOOT:
        DB      0DH, 0AH, 'Boot', 0DH, 0AH, 0

MSG_MPMLDR:
        DB      'LDR.', 0

MSG_LDRBIOS:
        DB      'IOS.', 0

MSG_READY:
        DB      'GO', 0DH, 0AH, 0

MSG_ERROR:
        DB      '?ERR', 0DH, 0AH, 0

; =============================================================================
; Pad to 512 bytes (one physical sector)
; =============================================================================

        DS      512 - ($ - START), 0

        END
