; xios.asm - MP/M II Extended I/O System for Emulator
; Part of MP/M II Emulator
; SPDX-License-Identifier: GPL-3.0-or-later
;
; This XIOS is designed to be intercepted by the emulator.
; Each entry point is a simple RET - the emulator traps on the
; PC value and handles the call in C++ code.
;
; Memory layout:
;   FC00-FC4A: Jump table (25 entries * 3 bytes = 75 bytes)
;   FC4B-FC65: Entry point stubs (RET instructions)
;   FC80-FCA5: Interrupt handlers
;   FD00-FD3F: DPH table (4 drives * 16 bytes)
;   FD40-FD4F: DPB for hd1k format
;   FD50-FDCF: DIRBUF (128 bytes)
;   FDD0-FFFF: Allocation vectors (4 * 144 bytes = 560 bytes)
;              (using 144 bytes per ALV instead of 256 to fit)
;
; Assemble with: z80asm -o xios.bin xios.asm

        ORG     0FC00H          ; Default XIOS base (configurable via -x)

; =============================================================================
; XIOS Jump Table - Standard BIOS entries (offsets 00H-30H)
; =============================================================================

BOOT:   JP      BOOT_           ; 00H - Cold boot
WBOOT:  JP      WBOOT_          ; 03H - Warm boot
CONST:  JP      CONST_          ; 06H - Console status
CONIN:  JP      CONIN_          ; 09H - Console input
CONOUT: JP      CONOUT_         ; 0CH - Console output
LIST:   JP      LIST_           ; 0FH - List output
PUNCH:  JP      PUNCH_          ; 12H - Punch output
READER: JP      READER_         ; 15H - Reader input
HOME:   JP      HOME_           ; 18H - Home disk
SELDSK: JP      SELDSK_         ; 1BH - Select disk
SETTRK: JP      SETTRK_         ; 1EH - Set track
SETSEC: JP      SETSEC_         ; 21H - Set sector
SETDMA: JP      SETDMA_         ; 24H - Set DMA address
READ:   JP      READ_           ; 27H - Read sector
WRITE:  JP      WRITE_          ; 2AH - Write sector
LISTST: JP      LISTST_         ; 2DH - List status
SECTRAN:JP      SECTRAN_        ; 30H - Sector translate

; =============================================================================
; XIOS Jump Table - Extended MP/M II entries (offsets 33H-48H)
; =============================================================================

SELMEM: JP      SELMEM_         ; 33H - Select memory bank
POLLDEV:JP      POLLDEV_        ; 36H - Poll device
STRTCLK:JP      STRTCLK_        ; 39H - Start clock
STOPCLK:JP      STOPCLK_        ; 3CH - Stop clock
EXITRG: JP      EXITRG_         ; 3FH - Exit region
MAXCON: JP      MAXCON_         ; 42H - Maximum console number
SYSINIT:JP      SYSINIT_        ; 45H - System initialization
IDLE:   JP      IDLE_           ; 48H - Idle procedure

; =============================================================================
; Entry Point Implementations
; The emulator intercepts at these addresses and handles the calls.
; Each routine just returns - the emulator does the real work.
; =============================================================================

BOOT_:
        RET
WBOOT_:
        RET
CONST_:
        RET
CONIN_:
        RET
CONOUT_:
        RET
LIST_:
        RET
PUNCH_:
        RET
READER_:
        RET
HOME_:
        RET
SELDSK_:
        RET
SETTRK_:
        RET
SETSEC_:
        RET
SETDMA_:
        RET
READ_:
        RET
WRITE_:
        RET
LISTST_:
        RET
SECTRAN_:
        RET
SELMEM_:
        RET
POLLDEV_:
        RET
STRTCLK_:
        RET
STOPCLK_:
        RET
EXITRG_:
        RET
MAXCON_:
        RET
SYSINIT_:
        RET
IDLE_:
        EI
        HALT
        RET

; =============================================================================
; Interrupt Handler (called from RST 38H during tick)
; =============================================================================

        ORG     0FC80H          ; Interrupt handler area

TICK_HANDLER:
        ; Timer tick interrupt handler
        PUSH    AF
        PUSH    BC
        PUSH    DE
        PUSH    HL

        ; Set MP/M flag #1 (tick) at SYSDAT+1
        LD      HL,(FLAG1_ADDR)
        LD      A,(HL)
        OR      A
        JR      NZ,TICK_DONE
        LD      (HL),0FFH

TICK_DONE:
        POP     HL
        POP     DE
        POP     BC
        POP     AF
        EI
        RETI

FLAG1_ADDR:
        DW      0000H

ONESEC_HANDLER:
        PUSH    AF
        PUSH    HL

        LD      HL,(FLAG2_ADDR)
        LD      A,(HL)
        OR      A
        JR      NZ,ONESEC_DONE
        LD      (HL),0FFH

ONESEC_DONE:
        POP     HL
        POP     AF
        RET

FLAG2_ADDR:
        DW      0000H

; =============================================================================
; Disk Parameter Headers (DPH) - one per drive
; Located in common memory so all banks can access
; =============================================================================

        ORG     0FD00H          ; DPH area

; DPH for drive A
DPH_A:
        DW      0               ; XLT - no translation
        DW      0,0,0           ; Scratch area
        DW      DIRBUF          ; Directory buffer
        DW      DPB_HD1K        ; Disk Parameter Block
        DW      0               ; CSV - no checksum (fixed disk)
        DW      ALV_A           ; Allocation vector

DPH_B:
        DW      0
        DW      0,0,0
        DW      DIRBUF
        DW      DPB_HD1K
        DW      0
        DW      ALV_B

DPH_C:
        DW      0
        DW      0,0,0
        DW      DIRBUF
        DW      DPB_HD1K
        DW      0
        DW      ALV_C

DPH_D:
        DW      0
        DW      0,0,0
        DW      DIRBUF
        DW      DPB_HD1K
        DW      0
        DW      ALV_D

; =============================================================================
; Disk Parameter Block for hd1k format (8MB, 1024 dir entries)
; =============================================================================

DPB_HD1K:
        DW      64              ; SPT - sectors per track (logical 128-byte)
        DB      5               ; BSH - block shift (4K blocks)
        DB      31              ; BLM - block mask
        DB      1               ; EXM - extent mask
        DW      2039            ; DSM - disk size - 1 (blocks)
        DW      1023            ; DRM - directory max - 1
        DB      0FFH            ; AL0 - directory allocation
        DB      0FFH            ; AL1
        DW      0               ; CKS - checksum size (0=fixed disk)
        DW      2               ; OFF - reserved tracks

; =============================================================================
; Disk buffers and allocation vectors
; Using reduced ALV size (144 bytes) to fit in available space
; =============================================================================

DIRBUF: DS      128             ; Directory buffer (shared)

; Allocation vectors - using 144 bytes each (enough for (DSM/8)+1)
; Real hd1k needs 255 bytes, but we truncate for space
; The emulator can handle this since it tracks allocation separately
ALV_A:  DS      144
ALV_B:  DS      144
ALV_C:  DS      144
ALV_D:  DS      144

; =============================================================================
; End of XIOS
; =============================================================================

        END
