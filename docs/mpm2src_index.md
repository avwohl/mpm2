# MP/M II Source File Index

Source files from `mpm2_external/mpm2src/` - the original Digital Research MP/M II source code.

## Directory Structure

| Directory | Contents |
|-----------|----------|
| NUCLEUS | Core OS kernel modules |
| MPMLDR | System loader and generation |
| BNKBDOS | Banked BDOS (disk OS) |
| CONTROL | XIOS/BIOS examples for OEM |
| UTIL1 | ASM assembler, DDT/RDT debugger |
| UTIL2 | RSP modules (MPMSTAT, SCHED, SPOOL) |
| UTIL3 | GENHEX, GENMOD, LOAD |
| UTIL4 | DIR, TYPE, ERA, REN, SET, SHOW, STAT |
| UTIL5 | ABORT, TOD, SUB, PRINT, STPSPL, etc |
| UTIL6 | PIP, ED |
| UTIL7 | SDIR (Super Directory) |
| UTIL8 | LIT include files |
| UTIL9 | Pre-built tools (MAC, RMAC, LIB, etc) |
| SERIAL | Serialization utility |
| PLM_WORK | PLM80 compiler work disk |
| TEX | Documentation source |

---

## NUCLEUS - OS Kernel

| Source File | Builds | Description |
|-------------|--------|-------------|
| MPM.ASM | XDOS.SPR | Main program - kernel entry and initialization |
| XDOS.ASM | XDOS.SPR | Extended Disk Operating System - BDOS call dispatcher |
| BNKXDOS.ASM | BNKXDOS.SPR | Banked XDOS - filename parsing and patching routines |
| DSPTCH.ASM | XDOS.SPR | Dispatcher - process scheduling and context switching |
| MEMMGR.ASM | XDOS.SPR | Memory Manager - bank allocation and management |
| QUEUE.ASM | XDOS.SPR | Queue Management - inter-process message queues |
| FLAG.ASM | XDOS.SPR | Flag Management - system flags and signaling |
| ATTACH.ASM | XDOS.SPR | Attach Process - console/list device attachment |
| CLI.ASM | XDOS.SPR | Command Line Interpreter - command parsing |
| CLOCK.ASM | XDOS.SPR | Clock Process - system time management |
| TICK.ASM | XDOS.SPR | Tick Process - timer interrupt handler |
| TH.ASM | XDOS.SPR | Terminal Handler - console I/O management |
| TMPSUB.ASM | XDOS.SPR | TMP subroutines - Terminal Message Processor support |
| BDOS30.ASM | XDOS.SPR | BDOS core - file system operations |
| BNKBDOS1.ASM | XDOS.SPR | Banked BDOS part 1 |
| RESBDOS1.ASM | XDOS.SPR | Resident BDOS part 1 |
| CONBDOS.ASM | XDOS.SPR | Console BDOS - console I/O functions |
| CLBDOS.ASM | XDOS.SPR | Close BDOS - file close operations |
| LST.ASM | XDOS.SPR | List device handling |
| RLSDEV.ASM | XDOS.SPR | Release Device - device detachment |
| RLSMX.ASM | XDOS.SPR | Release Mutual Exclusion |
| XDOSIF.ASM | XDOS.SPR | XDOS Interface |
| DATAPG.ASM | XDOS.SPR | Data Page - system data structures |
| PATCH.ASM | XDOS.SPR | Patch area |
| VER.ASM | XDOS.SPR | Version information |

---

## BNKBDOS - Banked BDOS

| Source File | Builds | Description |
|-------------|--------|-------------|
| BNKBDOS.ASM | BNKBDOS.SPR | Non-resident portion of Banked BDOS - disk I/O functions |

---

## MPMLDR - System Loader

| Source File | Builds | Description |
|-------------|--------|-------------|
| MPMLDR.PLM | MPMLDR.COM | MP/M II Loader - loads MPM.SYS from disk |
| GENSYS.PLM | GENSYS.COM | System Generation - creates MPM.SYS from SPR files |
| LDRBDOS.ASM | MPMLDR.COM | Loader BDOS - minimal BDOS for boot phase |
| LDRBIOS.ASM | (skeleton) | Loader BIOS skeleton - OEM customization required |
| LDRLWR.ASM | MPMLDR.COM | Loader low-level routines |
| LDMONX.ASM | MPMLDR.COM | Loader monitor interface |
| X0100.ASM | MPMLDR.COM | Loader initialization at 0100H |

---

## CONTROL - OEM XIOS Examples

| Source File | Builds | Description |
|-------------|--------|-------------|
| RESXIOS.ASM | BNKXIOS.SPR | DSC-2 Basic & Extended I/O System - full XIOS example |
| LDRBIOS.ASM | (OEM) | Loader BIOS for specific hardware |
| BOOT.ASM | (OEM) | Cold boot loader |
| DEBLOCK.ASM | BNKXIOS.SPR | Sector deblocking routines |
| DUMP.ASM | (OEM) | Memory dump utility |
| EXTRN.ASM | (OEM) | External declarations |
| TODCNV.ASM | BNKXIOS.SPR | Time-of-day conversion routines |

---

## UTIL1 - Assembler & Debugger

| Source File | Builds | Description |
|-------------|--------|-------------|
| AS0COM.ASM | ASM.PRL | ASM common data area |
| AS1IO.ASM | ASM.PRL | ASM I/O module |
| AS2SCAN.ASM | ASM.PRL | ASM scanner - tokenizer |
| AS3SYM.ASM | ASM.PRL | ASM symbol table handler |
| AS4SEAR.ASM | ASM.PRL | ASM symbol search |
| AS5OPER.ASM | ASM.PRL | ASM operand processor |
| AS6MAIN.ASM | ASM.PRL | ASM main program |
| DDT0MOV.ASM | RDT.PRL | DDT move module |
| DDT1ASM.ASM | RDT.PRL | DDT inline assembler/disassembler |
| DDT2MON.ASM | RDT.PRL | DDT debugger monitor (DEMON) |

---

## UTIL2 - RSP Modules

| Source File | Builds | Description |
|-------------|--------|-------------|
| MSBRS.PLM | MPMSTAT.RSP | MPMSTAT - banked portion (system status display) |
| MSRSP.PLM | MPMSTAT.RSP | MPMSTAT - RSP portion |
| MSCMN.PLM | (common) | MPMSTAT common routines |
| SCBRS.PLM | SCHED.RSP | SCHED - banked portion (scheduler configuration) |
| SCRSP.PLM | SCHED.RSP | SCHED - RSP portion |
| SPBRS.PLM | SPOOL.RSP | SPOOL - banked portion (print spooler) |
| SPRSP.PLM | SPOOL.RSP | SPOOL - RSP portion |
| ABORT.ASM | ABORT.RSP | Abort RSP handler |
| BRSPBI.ASM | (common) | RSP BIOS interface |

---

## UTIL3 - Code Generation Tools

| Source File | Builds | Description |
|-------------|--------|-------------|
| LOAD.PLM | LOAD.PRL | HEX file loader - loads .HEX to .COM |
| GENHEX.ASM | GENHEX.COM | Generate HEX - converts binary to Intel HEX |
| GENMOD.ASM | GENMOD.COM | Generate Module - creates relocatable .PRL/.RSP/.SPR |

---

## UTIL4 - File Utilities

| Source File | Builds | Description |
|-------------|--------|-------------|
| DIR.PLM | DIR.PRL | Directory listing |
| ERA.PLM | ERA.PRL | Erase files |
| ERAQ.PLM | ERAQ.PRL | Erase with query confirmation |
| REN.PLM | REN.PRL | Rename files |
| TYPE.PLM | TYPE.PRL | Display file contents |
| STAT.PLM | STAT.PRL | File/disk statistics |
| SET.PLM | SET.PRL | Set file attributes |
| SHOW.PLM | SHOW.PRL | Show disk/user info |

---

## UTIL5 - System Utilities

| Source File | Builds | Description |
|-------------|--------|-------------|
| ABORT.PLM | ABORT.PRL | Abort a running program |
| TOD.PLM | TOD.PRL | Time-of-day display/set |
| SUB.PLM | SUB.PRL | Submit batch processor |
| PRINT.PLM | PRINTER.PRL | List device assignment utility |
| MSCHD.PLM | SCHED.PRL | Scheduler transient program |
| MSTS.PLM | MPMSTAT.PRL | Status display transient |
| MSPL.PLM | SPOOL.PRL | Spool control transient |
| STPSP.PLM | STPSPL.PRL | Stop spooler |
| DRST.PLM | DSKRESET.PRL | Disk reset |
| CNS.PLM | CONSOLE.PRL | Console status/switch |
| USER.PLM | USER.PRL | User number display/set |
| PRLCM.PLM | PRLCOM.PRL | Convert PRL to COM |
| DUMP.ASM | DUMP.PRL | Memory dump |
| EXTRN.ASM | (common) | External declarations |

---

## UTIL6 - Text Processing

| Source File | Builds | Description |
|-------------|--------|-------------|
| PIP.PLM | PIP.PRL | Peripheral Interchange Program - file copy/transfer |
| ED.PLM | ED.PRL | Line editor |

---

## UTIL7 - Super Directory (SDIR)

| Source File | Builds | Description |
|-------------|--------|-------------|
| DM.PLM | SDIR.PRL | SDIR main module |
| SN.PLM | SDIR.PRL | SDIR screen output |
| DSE.PLM | SDIR.PRL | SDIR directory search engine |
| DSH.PLM | SDIR.PRL | SDIR search helpers |
| DSO.PLM | SDIR.PRL | SDIR sort operations |
| DA.PLM | SDIR.PRL | SDIR arithmetic routines |
| DP.PLM | SDIR.PRL | SDIR print formatting |
| DTS.PLM | SDIR.PRL | SDIR time/size display |

---

## UTIL8 - Include Files (.LIT)

| File | Description |
|------|-------------|
| COMMON.LIT | Common literals and macros |
| PROCES.LIT | Process descriptor structure |
| QUEUE.LIT | Queue structure definitions |
| MEMMGR.LIT | Memory manager structures |
| FLAG.LIT | Flag definitions |
| FCB.LIT | File Control Block structure |
| XDOS.LIT | XDOS function numbers |
| SYSDAT.LIT | System data page layout |
| DPGOS.LIT | Data page OS definitions |
| COPYRT.LIT | Copyright notice |

---

## UTIL9 - Pre-built Tools

These are pre-compiled binaries, not source:

| File | Description |
|------|-------------|
| MAC.COM | MACRO-80 assembler |
| RMAC.COM | Relocating MACRO assembler |
| LINK.COM | Linker |
| LIB.COM | Library manager |
| XREF.COM | Cross-reference generator |
| SUBMIT.COM | Batch file processor |
| GENHEX.COM | Generate Intel HEX |
| GENMOD.COM | Generate relocatable module |
| PRLCOM.COM | Convert PRL to COM |
| LOAD.COM | HEX loader |
| PIP.COM | File copy utility |
| COMPARE.COM | File comparison |
| ZERO.COM | Zero fill utility |

---

## SERIAL - Serialization

| Source File | Builds | Description |
|-------------|--------|-------------|
| MPM2SRL.ASM | (internal) | Serialization program for MP/M II diskettes |

---

## PLM_WORK - Compiler Tools

| File | Description |
|------|-------------|
| PLM80 | Intel PL/M-80 compiler |
| PLM80.OV0-4 | Compiler overlays |
| PLM80.LIB | PL/M runtime library |
| ASM80 | Intel 8080 assembler |
| LINK | Intel linker |
| LINK.OVL | Linker overlay |
| DISKDEF.LIB | Disk definition macros |
| Z80S.LIB | Z80 instruction set library |

---

## Notes

- **PL/M-80**: Most utilities are written in Intel PL/M-80, a high-level language for 8080/Z80
- **ASM**: Kernel modules are in 8080/Z80 assembly (originally PL/M, converted to ASM)
- **.PRL**: Page Relocatable format - position-independent executables
- **.RSP**: Resident System Process - background processes loaded at boot
- **.SPR**: System Page Relocatable - kernel modules
- **.LIT**: Literal include files for PL/M-80

## Build Chain

1. PL/M-80 source (.PLM) -> PLM80 compiler -> .OBJ
2. Assembly source (.ASM) -> ASM80/MAC/RMAC -> .REL/.OBJ
3. Object files -> LINK -> absolute binary
4. Binary -> GENMOD -> .PRL/.RSP/.SPR relocatable

## Copyright

All source files are Copyright (C) 1979-1982 Digital Research, Inc.
