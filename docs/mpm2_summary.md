# MP/M II Command Reference

MP/M II (Multi-Programming Monitor) V2.1 - Digital Research, 1982

## Disk File Index

| File | Type | Size | Description |
|------|------|------|-------------|
| ABORT.PRL | Command | 640 | Terminate running program |
| ASM.PRL | Command | 9KB | 8080 assembler |
| CONSOLE.PRL | Command | 512 | Display console number |
| DDT.COM | Command | 5KB | Dynamic Debugging Tool |
| DIR.PRL | Command | 2KB | Directory listing |
| DSKRESET.PRL | Command | 640 | Reset disk drives |
| DUMP.PRL | Command | 768 | Hex file dump |
| ED.PRL | Command | 9KB | Line editor |
| ERA.PRL | Command | 2KB | Erase files |
| ERAQ.PRL | Command | 4KB | Erase with query |
| GENHEX.COM | Utility | 768 | COM to HEX converter |
| GENMOD.COM | Utility | 1KB | Generate PRL from HEX |
| GENSYS.COM | Utility | 9KB | System generator |
| LIB.COM | Utility | 7KB | Library manager |
| LINK.COM | Utility | 16KB | Object linker |
| LOAD.COM | Utility | 2KB | HEX to COM loader |
| MPMSTAT.PRL | Command | 4KB | System status display |
| PIP.PRL | Command | 10KB | File copy utility |
| PRINTER.PRL | Command | 1KB | Set printer device |
| PRLCOM.PRL | Utility | 3KB | PRL to COM converter |
| RDT.PRL | Command | 6KB | Relocatable debugger |
| REN.PRL | Command | 2KB | Rename files |
| RMAC.COM | Utility | 14KB | Relocatable assembler |
| SCHED.PRL | Command | 3KB | Schedule commands |
| SDIR.PRL | Command | 18KB | Extended directory |
| SET.PRL | Command | 8KB | Set file attributes |
| SHOW.PRL | Command | 8KB | Show disk label |
| SPOOL.PRL | Command | 2KB | Print spooler |
| STAT.PRL | Command | 10KB | File/disk statistics |
| STOPSPLR.PRL | Command | 640 | Stop spooler |
| SUBMIT.PRL | Command | 5KB | Batch command processor |
| TOD.PRL | Command | 3KB | Time of day |
| TYPE.PRL | Command | 1KB | Display file contents |
| USER.PRL | Command | 1KB | Set user number |
| XREF.COM | Utility | 15KB | Cross-reference |

### System Files

| File | Description |
|------|-------------|
| MPM.SYS | MP/M II operating system |
| MPMLDR.COM | System loader |
| BNKBDOS.SPR | Banked BDOS |
| BNKXDOS.SPR | Banked XDOS extension |
| RESBDOS.SPR | Resident BDOS |
| XDOS.SPR | Extended DOS |
| TMP.SPR | Terminal Message Processor |

### Assembly Source & Libraries

| File | Description |
|------|-------------|
| BOOT.ASM | Boot loader source |
| DEBLOCK.ASM | Sector deblocking source |
| LDRBIOS.ASM | Loader BIOS source |
| RESXIOS.ASM | Resident XIOS source |
| TODCNV.ASM | Time conversion source |
| DISKDEF.LIB | Disk definition macros |
| Z80.LIB | Z80 instruction macros |

---

## Command Reference

### File Management

**DIR** - Directory listing
```
DIR [d:][filespec] [SYS] [Gn]
```
Display directory. `SYS` shows system files. `Gn` specifies user area.

**ERA** - Erase files
```
ERA filespec
```
Delete files. Supports wildcards (*.*, *.TXT).

**ERAQ** - Erase with confirmation
```
ERAQ filespec
```
Delete files with Y/N prompt for each file.

**REN** - Rename file
```
REN newname.typ=oldname.typ
```
Rename a file. Supports wildcards if patterns match.

**TYPE** - Display file
```
TYPE filespec [PAGE]
```
Display ASCII file contents. `PAGE` pauses every 24 lines.

**SDIR** - Extended directory
```
SDIR [d:][filespec] [options]
```
Full-featured directory with sizes, dates, attributes.

**SET** - Set attributes
```
SET filespec [RO|RW|SYS|DIR|ARCHIVE]
```
Set file attributes: read-only, system, archive flag.

**STAT** - Statistics
```
STAT [d:][filespec] [DSK:]
```
Show disk space, file sizes, drive characteristics.

### File Transfer

**PIP** - Peripheral Interchange Program
```
PIP dest=source [options]
```
Copy files between drives, users, devices. Examples: `PIP B:=A:*.PRL`, `PIP FILE.TXT=CON:`. Options:
- `[Gn]` - user area
- `[V]` - verify
- `[R]` - read system files

### Process Control

**ABORT** - Terminate program
```
ABORT programname [console]
```
Stop a running program. Optional console number for remote abort.

**ATTACH** - Reattach process
```
ATTACH programname
```
Reconnect a detached process to current console.

**CONSOLE** - Show console
```
CONSOLE
```
Display current console number.

**USER** - User area
```
USER [n]
```
Display or set user number (0-15).

**MPMSTAT** - System status
```
MPMSTAT
```
Display processes, queues, memory, device assignments.

### Scheduling & Printing

**SCHED** - Schedule command
```
SCHED mm/dd/yy hh:mm command
```
Execute command at specified date/time.

**SPOOL** - Print spooler
```
SPOOL filespec [DELETE]
```
Queue file for printing. `DELETE` removes after printing.

**STOPSPLR** - Stop spooler
```
STOPSPLR
```
Cancel current print job and clear queue.

**PRINTER** - Set printer
```
PRINTER [n]
```
Display or set printer device number.

### System Utilities

**TOD** - Time of day
```
TOD [mm/dd/yy hh:mm:ss]
```
Display or set system clock. `TOD PERPETUAL` for continuous display.

**DSKRESET** - Reset drives
```
DSKRESET [d: ...]
```
Reset disk drives. No arguments resets all.

**SHOW** - Disk label
```
SHOW [LABEL]
```
Display disk label and protection status.

**SUBMIT** - Batch processing
```
SUBMIT filename [$1 $2 ...]
```
Execute commands from .SUB file with parameter substitution.

### Development Tools

**ED** - Text editor
```
ED filespec [d:]
```
Line-oriented editor. Creates .BAK backup.

**ASM** - Assembler
```
ASM filename
```
8080 assembler producing .HEX and .PRN files.

**RMAC** - Relocatable assembler
```
RMAC filename
```
Produces relocatable .REL object files.

**LINK** - Linker
```
LINK
```
Link .REL files into executables or .SPR files.

**LOAD** - Loader
```
LOAD filename
```
Convert .HEX to .COM executable.

**DDT** - Debugger
```
DDT [filespec]
```
Debug .COM files. CP/M compatible.

**RDT** - Relocatable debugger
```
RDT [filespec]
```
Debug both .COM and .PRL files.

**DUMP** - Hex dump
```
DUMP filename.typ
```
Display file in hexadecimal format.

**GENHEX** - Generate HEX
```
GENHEX filename offset
```
Convert .COM to Intel .HEX format.

**GENMOD** - Generate PRL
```
GENMOD file.hex file.prl
```
Create relocatable .PRL from .HEX files.

**PRLCOM** - Convert PRL
```
PRLCOM file.prl file.com
```
Convert .PRL to non-relocatable .COM.

**LIB** - Library manager
```
LIB
```
Create and manage .LIB library files.

**XREF** - Cross-reference
```
XREF
```
Generate cross-reference listing from assembly.

---

## Console Control Keys

| Key | Function |
|-----|----------|
| ^C | Abort program |
| ^S | Pause output |
| ^Q | Resume output |
| ^P | Toggle printer echo |
| ^H | Backspace |
| ^X | Delete line |
| ^R | Retype line |

## File Types

| Extension | Description |
|-----------|-------------|
| .COM | Executable (absolute) |
| .PRL | Page Relocatable (MP/M) |
| .SPR | System Page Relocatable |
| .RSP | Resident System Process |
| .BRS | Banked RSP |
| .REL | Relocatable object |
| .HEX | Intel hex format |
| .SUB | Submit batch file |
| .SUP | Startup file |
| .BAK | Backup file |
| .PRN | Print/listing file |
| .SYM | Symbol file |
| .LIB | Library file |

---

*Generated from MP/M II V2.1 distribution and documentation*
