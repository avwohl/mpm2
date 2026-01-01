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
DIR [d:][filespec] [options]
```
Display directory.

| Option | Description |
|--------|-------------|
| `[SYS]` | Show system files |
| `[Gn]` | Display files from user area n (0-15) |

Examples:
```
DIR              ; List files in current user area
DIR [SYS]        ; Include system files
DIR *.ASM [G8]   ; List ASM files from user 8
DIR *.ASM, *.LIB ; Multiple file specifications
```

---

**SDIR** - Extended directory
```
SDIR [options] [d:][filespec]
```
Full-featured directory with sizes, dates, attributes, sorting.

| Option | Description |
|--------|-------------|
| `[SYS]` | Display only system files |
| `[DIR]` | Display only non-system files |
| `[RO]` | Display only read-only files |
| `[RW]` | Display only read-write files |
| `[XFCB]` | Display files with extended FCBs |
| `[NONXFCB]` | Display files without extended FCBs |
| `[USER=n]` | Display files from user n |
| `[USER=ALL]` | Display files from all user areas |
| `[USER=(0,1,...)]` | Display files from specified users |
| `[DRIVE=d]` | Display files from drive d |
| `[DRIVE=ALL]` | Display files from all logged drives |
| `[DRIVE=(A,B,...)]` | Display files from specified drives |
| `[FULL]` | Full format display (default) |
| `[SIZE]` | Display file sizes in kilobytes |
| `[SHORT]` | Short format, 4 columns, unsorted |
| `[NOSORT]` | Display files unsorted |
| `[EXCLUDE]` | Display files NOT matching filespec |
| `[MESSAGE]` | Show drive/user headers even if empty |
| `[LENGTH=n]` | Lines per page (5-65536) |
| `[FF]` | Send form-feed for printer output |
| `[HELP]` | Display help examples |

Examples:
```
SDIR [SIZE] D:                           ; Show file sizes on D:
SDIR [USER=ALL,DRIVE=ALL,SYS] *.PRL     ; Find all system PRL files
SDIR [SHORT] A: B: C:                    ; Quick listing of 3 drives
SDIR [EXCLUDE] *.COM *.PRL               ; Files except COM and PRL
```

---

**ERA** - Erase files
```
ERA filespec [options]
```
Delete files. Supports wildcards.

| Option | Description |
|--------|-------------|
| `[XFCB]` | Erase only extended FCBs (passwords/timestamps), not file |

Examples:
```
ERA OLDFILE.TXT          ; Delete single file
ERA *.BAK                ; Delete all BAK files
ERA B:*.*                ; Delete all files on B: (prompts for confirmation)
ERA MYFILE.TXT [XFCB]    ; Remove password/timestamps only
```

---

**ERAQ** - Erase with confirmation
```
ERAQ filespec [options]
```
Delete files with Y/N prompt for each file.

| Option | Description |
|--------|-------------|
| `[XFCB]` | Erase only extended FCBs |

---

**REN** - Rename file
```
REN newname.typ = oldname.typ
REN newname.typ = d:oldname.typ;password
```
Rename a file. Supports wildcards if patterns match.

---

**TYPE** - Display file
```
TYPE filespec [options]
```
Display ASCII file contents.

| Option | Description |
|--------|-------------|
| `[PAGE]` | Pause every 24 lines |
| `[Pn]` | Pause every n lines |

Examples:
```
TYPE README.TXT           ; Display file
TYPE README.TXT [PAGE]    ; Paginated display
TYPE README.TXT [P60]     ; Pause every 60 lines
```

---

**SET** - Set file/disk attributes
```
SET [options]
SET d:[options]
SET filespec [options]
```
Set file attributes, passwords, and time stamping.

**Drive Options:**

| Option | Description |
|--------|-------------|
| `[RO]` | Set drive to read-only |
| `[RW]` | Set drive to read-write |
| `[PASSWORD=pwd]` | Set directory label password |
| `[PROTECT=ON/OFF]` | Enable/disable password protection |
| `[NAME=name.typ]` | Set disk label name |
| `[CREATE=ON/OFF]` | Enable creation timestamps |
| `[ACCESS=ON/OFF]` | Enable access timestamps |
| `[UPDATE=ON/OFF]` | Enable update timestamps |
| `[MAKE=ON/OFF]` | Auto-create XFCBs for new files |

**File Options:**

| Option | Description |
|--------|-------------|
| `[RO]` | Set file read-only |
| `[RW]` | Set file read-write |
| `[SYS]` | Set system attribute (accessible from any user) |
| `[DIR]` | Set directory attribute (normal file) |
| `[PASSWORD=pwd]` | Assign password to file |
| `[PROTECT=READ]` | Password required for all access |
| `[PROTECT=WRITE]` | Password required to modify |
| `[PROTECT=DELETE]` | Password required to delete/rename |
| `[PROTECT=NONE]` | Remove password protection |
| `[TIME]` | Create XFCB for timestamps |
| `[ARCHIVE=ON/OFF]` | Set/clear archive attribute |
| `[F1=ON/OFF]` | User-definable attribute 1 |
| `[F2=ON/OFF]` | User-definable attribute 2 |
| `[F3=ON/OFF]` | User-definable attribute 3 |
| `[F4=ON/OFF]` | User-definable attribute 4 |
| `[DEFAULT=pwd]` | Set default password for console |
| `[HELP]` | Display help |

Examples:
```
SET [PASSWORD=SECRET]                     ; Set label password
SET [PROTECT=ON]                          ; Enable password protection
SET [CREATE=ON,UPDATE=ON]                 ; Enable timestamps
SET *.PRL [SYS,RO]                        ; Make PRL files system/read-only
SET MYFILE.TXT [PASSWORD=ABC,PROTECT=READ] ; Password protect file
SET [DEFAULT=MYPASS]                      ; Set default password
```

---

**STAT** - Statistics
```
STAT [d:][filespec] [options]
STAT d:=RO
STAT DSK:
STAT USR:
STAT VAL:
```
Show disk space, file sizes, drive characteristics.

| Option/Command | Description |
|----------------|-------------|
| `[SIZE]` | Show computed file size |
| `[RO]` | Set file read-only |
| `[RW]` | Set file read-write |
| `[SYS]` | Set file system attribute |
| `[DIR]` | Set file directory attribute |
| `d:=RO` | Set drive to read-only |
| `DSK:` | Show disk characteristics |
| `d:DSK:` | Show specific drive characteristics |
| `USR:` | Show active user areas |
| `VAL:` | Display valid STAT commands |

Examples:
```
STAT                ; Show space on logged drives
STAT B:             ; Show space on drive B
STAT *.PRL          ; Show PRL file statistics
STAT *.PRL [SIZE]   ; Include computed file sizes
STAT B:DSK:         ; Show drive B characteristics
STAT USR:           ; Show which user areas have files
```

---

**SHOW** - Disk information
```
SHOW [option]
SHOW d:[option]
```
Display disk and label information.

| Option | Description |
|--------|-------------|
| `SPACE` | Show free space (default) |
| `DRIVES` | Show drive characteristics |
| `USERS` | Show active user areas |
| `LABEL` | Show directory label info |
| `HELP` | Display options |

Examples:
```
SHOW                ; Show space on all logged drives
SHOW B:             ; Show space on drive B
SHOW DRIVES         ; Show drive characteristics
SHOW B:LABEL        ; Show label for drive B
```

---

### File Transfer

**PIP** - Peripheral Interchange Program
```
PIP dest=source[options]
PIP dest=source1,source2[options]
```
Copy files between drives, users, devices.

| Option | Description |
|--------|-------------|
| `[A]` | Archive - copy only modified files, set archive bit |
| `[Dn]` | Delete characters past column n |
| `[E]` | Echo transfer to console |
| `[F]` | Filter (remove) form-feeds |
| `[Gn]` | Get from/go to user area n |
| `[H]` | Hex file transfer with validation |
| `[I]` | Ignore :00 records in HEX files |
| `[K]` | Kill filename display during copy |
| `[L]` | Translate to lowercase |
| `[N]` | Add line numbers |
| `[N2]` | Add line numbers with leading zeros and tab |
| `[O]` | Object file transfer (ignore EOF) |
| `[Pn]` | Insert page breaks every n lines (default 60) |
| `[Qs^Z]` | Quit copying after string s |
| `[R]` | Read system files |
| `[Ss^Z]` | Start copying from string s |
| `[Tn]` | Expand tabs to n columns |
| `[U]` | Translate to uppercase |
| `[V]` | Verify copy |
| `[W]` | Write over read-only files |
| `[Z]` | Zero parity bit |

Examples:
```
PIP B:=A:*.PRL                    ; Copy all PRL files
PIP B:=A:FILE.TXT[V]              ; Copy with verify
PIP B:=A:*.* [G3,R]               ; Copy from user 3, include SYS files
PIP B:FILE.TXT=A:FILE.TXT[G5]     ; Copy to user 5
PIP LST:=B:FILE.TXT[T8,U]         ; Print with tabs expanded, uppercase
PIP CON:=A:FILE.ASM[D80]          ; Display truncated to 80 columns
PIP B:=A:*.TXT[A,V]               ; Backup modified files with verify
PIP OUT.TXT=IN.TXT[SDear^Z,QEnd^Z] ; Extract text between markers
PIP LST:=FILE.ASM[N2,T8,P65]      ; Print with line numbers, tabs, pagination
```

---

### Process Control

**ABORT** - Terminate program
```
ABORT programname
ABORT programname n
```
Stop a running program.

| Argument | Description |
|----------|-------------|
| `programname` | Name of program to abort |
| `n` | Console number (for remote abort) |

Examples:
```
ABORT TYPE          ; Abort TYPE on current console (must detach first)
ABORT TYPE 1        ; Abort TYPE running on console 1
```

---

**ATTACH** - Reattach process
```
ATTACH programname
```
Reconnect a detached process to current console.

---

**CONSOLE** - Show console
```
CONSOLE
```
Display current console number.

---

**USER** - User area
```
USER [n]
```
Display or set user number (0-15).

---

**MPMSTAT** - System status
```
MPMSTAT
```
Display processes, queues, memory allocation, device assignments.

---

### Scheduling & Printing

**SCHED** - Schedule command
```
SCHED mm/dd/yy hh:mm command
```
Execute command at specified date/time.

Example:
```
SCHED 12/25/82 00:01 SUBMIT BACKUP    ; Run backup at midnight on Christmas
```

---

**SPOOL** - Print spooler
```
SPOOL filespec [options]
SPOOL filespec, filespec, ...
```
Queue file(s) for printing.

| Option | Description |
|--------|-------------|
| `[DELETE]` | Delete file after printing |

Examples:
```
SPOOL REPORT.TXT                ; Queue for printing
SPOOL REPORT.TXT [DELETE]       ; Print then delete
SPOOL *.PRN                     ; Queue multiple files
```

---

**STOPSPLR** - Stop spooler
```
STOPSPLR
```
Cancel current print job and clear queue.

---

**PRINTER** - Set printer
```
PRINTER [n]
```
Display or set printer device number.

---

### System Utilities

**TOD** - Time of day
```
TOD
TOD PERPETUAL
TOD mm/dd/yy hh:mm:ss
```
Display or set system clock.

| Form | Description |
|------|-------------|
| `TOD` | Display current date/time |
| `TOD PERPETUAL` | Continuous time display |
| `TOD mm/dd/yy hh:mm:ss` | Set date and time |

---

**DSKRESET** - Reset drives
```
DSKRESET
DSKRESET d:
DSKRESET d:,d:,d:
```
Reset disk drives for disk change.

| Form | Description |
|------|-------------|
| `DSKRESET` | Reset all drives |
| `DSKRESET d:` | Reset specific drive |
| `DSKRESET A:,B:` | Reset multiple drives |

---

**SUBMIT** - Batch processing
```
SUBMIT filename
SUBMIT filename $1 $2 $3 ...
```
Execute commands from .SUB file with parameter substitution.

| Parameter | Description |
|-----------|-------------|
| `$1-$9` | Substitution parameters |
| `$$` | Literal dollar sign |

Example SUB file (BACKUP.SUB):
```
ERA $1:*.BAK
PIP $1:=A:*.* [V]
DIR $1:
```
Usage: `SUBMIT BACKUP B` substitutes B for $1.

---

### Development Tools

**ED** - Text editor
```
ED filespec
ED filespec d:
```
Line-oriented editor. Creates .BAK backup.

ED Commands (single letter commands, n = optional count):

| Command | Description |
|---------|-------------|
| `nA` | Append n lines from file to buffer |
| `B` | Move to beginning of buffer |
| `-B` | Move to end of buffer (same as Z) |
| `nC` | Move n characters forward |
| `-nC` | Move n characters backward |
| `nD` | Delete n characters forward |
| `-nD` | Delete n characters backward |
| `E` | End edit, save file |
| `Fstring^Z` | Find string |
| `H` | End edit with backup, re-edit |
| `I` | Enter insert mode |
| `Itext^Z` | Insert text |
| `nK` | Kill (delete) n lines |
| `nL` | Move n lines forward |
| `-nL` | Move n lines backward |
| `nMcommand` | Execute command n times (macro) |
| `Nstring^Z` | Find string, extending to disk |
| `O` | Return to original file |
| `nP` | Display n pages (23 lines) |
| `Q` | Quit, abandon changes |
| `R` | Read library file |
| `Sold^Znew^Z` | Substitute new for old |
| `nT` | Type n lines |
| `U` | Uppercase mode toggle |
| `V` | Line number display toggle |
| `nW` | Write n lines to output |
| `nX` | Transfer n lines to/from X buffer |
| `Z` | Move to end of buffer |
| `n` | Move to line n |
| `n:` | Move to character position n |

---

**ASM** - Assembler
```
ASM filename
ASM filename.ppp
```
8080 assembler producing .HEX and .PRN files.

| Parameter | Description |
|-----------|-------------|
| `.ppp` | Override source/hex/print drives (e.g., ASM FILE.ABZ) |

---

**RMAC** - Relocatable assembler
```
RMAC filename
RMAC filename $options
```
Produces relocatable .REL object files.

| Option | Description |
|--------|-------------|
| `$PL` | Print listing |
| `$PP` | Print to printer |
| `$SZ` | Symbol table to file |

---

**LINK** - Linker
```
LINK filename
LINK filename,filename,...
LINK filename [options]
```
Link .REL files into executables or .SPR files.

---

**LOAD** - Loader
```
LOAD filename
```
Convert .HEX to .COM executable.

---

**DDT** - Debugger
```
DDT
DDT filespec
```
Debug .COM files. CP/M compatible.

---

**RDT** - Relocatable debugger
```
RDT
RDT filespec
```
Debug both .COM and .PRL files.

---

**DUMP** - Hex dump
```
DUMP filename.typ
```
Display file in hexadecimal format.

---

**GENHEX** - Generate HEX
```
GENHEX filename offset
```
Convert .COM to Intel .HEX format at specified offset.

---

**GENMOD** - Generate PRL
```
GENMOD file1.hex file2.hex outfile.prl
```
Create relocatable .PRL from two .HEX files (offset 0 and 100H).

---

**PRLCOM** - Convert PRL
```
PRLCOM file.prl file.com
```
Convert .PRL to non-relocatable .COM.

---

**LIB** - Library manager
```
LIB
```
Create and manage .LIB library files (interactive).

---

**XREF** - Cross-reference
```
XREF
```
Generate cross-reference listing from assembly (interactive).

---

## Console Control Keys

| Key | Function |
|-----|----------|
| ^C | Abort program (prompts Y/N) |
| ^D | Detach/attach process |
| ^S | Pause output |
| ^Q | Resume output |
| ^P | Toggle printer echo |
| ^H | Backspace |
| ^X | Delete line |
| ^R | Retype line |
| ^U | Delete line (alternate) |

---

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

## Password Protection Modes

| Mode | Protection Level |
|------|-----------------|
| READ | Password required for all access |
| WRITE | Password required for write/delete/rename |
| DELETE | Password required for delete/rename only |
| NONE | No password protection |

---

## User Areas

MP/M II supports 16 user areas (0-15). Each console starts in a different user area:
- Console 0 -> User 0
- Console 1 -> User 1
- etc.

Files with SYS attribute in user 0 are accessible from all user areas.

---

*Generated from MP/M II V2.1 distribution and documentation*
