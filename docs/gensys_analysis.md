# GENSYS Analysis

Analysis of the MP/M II System Generation utility based on source code at:
`mpm2_external/mpm2src/MPMLDR/GENSYS.PLM`

## Purpose

GENSYS creates MPM.SYS by:
1. Gathering system configuration from the user
2. Loading and relocating SPR modules (RESBDOS, XDOS, BNKXIOS, BNKBDOS, TMP, etc.)
3. Writing the combined system to MPM.SYS
4. Writing configuration to SYSTEM.DAT file

## External Dependencies

GENSYS relies on external assembly procedures from `LDRLWR.ASM`:

| Procedure | Purpose |
|-----------|---------|
| `Ld$Rl` | Load and relocate SPR file into memory buffer |
| `Fx$Wr` | Fix up and write relocated code to output file |
| `mon1/mon2` | BDOS function calls |

### LdRl - Load and Relocate (LDRLWR.ASM lines 26-125)

1. Computes sectors to read: `(prgsiz + 127) / 128`
2. Reads SPR file sector-by-sector into `sctbfr` using BDOS 20
3. Reads relocation bitmap from remaining file data
4. For each byte in program: if corresponding bitmap bit = 1, add `offset` to byte
5. Returns A=0 on success, A=0xFF on error

### FxWr - Fix and Write (LDRLWR.ASM lines 128-189)

1. Pre-fills a 128-byte buffer with zeros (for padding)
2. If `bufsiz > 0`, writes data buffer sectors first
3. **Writes program sectors TOP to BOTTOM** (addresses decreasing):
   ```asm
   Wrloop:
       call    write       ; BDOS 21 - write sequential
       lhld    DMA
       lxi     d,-0080h    ; subtract 128 from DMA
       dad     d
       ...
       jnz     Wrloop
   ```
4. Uses BDOS function 21 (write sequential) for all writes

**Critical**: If BDOS 21 (write sequential) doesn't work correctly, MPM.SYS will be incomplete.

## Main Program Flow

Entry point at `start:` (line 1192):

```
1. initialization          - Calculate buffer size, check for automatic mode
2. setup$MPM$sys          - Print signon, create empty MPM.SYS file
3. setup$system$dat       - Get user configuration, build SYSTEM.DAT
4. write$preamble         - Pre-allocate space in MPM.SYS with zeros
5. Load SPR files:
   a. RESBDOS.SPR         - Resident BDOS
   b. XDOS.SPR            - Extended DOS
   c. RSPs (*.RSP)        - Resident System Processes (optional)
   d. BNKXIOS.SPR         - Banked XIOS (or RESXIOS.SPR if non-banked)
   e. BNKBDOS.SPR         - Banked BDOS
   f. BNKXDOS.SPR         - Banked XDOS
   g. TMP.SPR             - Terminal Message Processor (if consoles > 0)
6. setup$mem$seg$tbl      - Configure memory segment table
7. write$system$dat       - Write final SYSTEM.DAT to MPM.SYS and SYSTEM.DAT file
8. Print "** GENSYS DONE **"
9. system$reset           - Return to CP/M
```

## Key Procedures

### write$preamble (line 951)

Pre-allocates space in MPM.SYS by writing zeroed 128-byte records:

```plm
i = (mem$top - cur$base + 1) * 2 + 1;
do while (i := i - 1) <> 0;
    call write$record (.FCBout);
end;
```

This writes enough records to cover from `cur_base` to `mem_top` (the entire system area).

### load$reloc (line 799)

Loads an SPR file using the external `Ld$Rl` procedure:

1. Open the SPR file
2. Read header record (contains psize and dsize)
3. Verify it fits in buffer
4. Calculate new `cur_base` (decreasing from `mem_top`)
5. Call `Ld$Rl` to load and relocate the file
6. Display the module name, base address, and size

The SPR header format (first 128 bytes):
- Byte 1: (fill)
- Bytes 2-3: `psize` - Program/code size
- Byte 4: (fill)
- Bytes 5-6: `dsize` - Data/buffer size
- Rest: (fill/relocation info)

### Fx$Wr (external)

After `Ld$Rl` loads and relocates code into the buffer, `Fx$Wr` writes it to the output file. This is called after loading each SPR:

```plm
call load$reloc (.('RESBDOS SPR$'), .resbdos$base);
call Fx$Wr;                                          /* Write to file */
```

### write$system$dat (line 610)

Finalizes MPM.SYS by:
1. Computing file size (nmb$records)
2. Using random record writes to update specific records:
   - Records 0-1: SYSTEM.DAT (256 bytes)
   - XIOS jump table record
   - Common XDOS record (patching jump table)
3. Also writes to separate SYSTEM.DAT file

## Memory Layout During Generation

GENSYS loads modules **downward** from `mem_top`:

```
mem_top (FFH)  ┌─────────────────┐
               │   SYSTEM.DAT    │  (reserved, 1 page)
               ├─────────────────┤
               │   TMPD.DAT      │  (if consoles > 0)
               ├─────────────────┤
               │   USERSYS.STK   │  (if sys_call_stks)
               ├─────────────────┤
               │   XIOSJMP TBL   │  (1 page)
               ├─────────────────┤
               │   RESBDOS.SPR   │  ← First SPR loaded
               ├─────────────────┤
               │   XDOS.SPR      │
               ├─────────────────┤
               │   RSPs          │  (optional)
               ├─────────────────┤
               │   BNKXIOS.SPR   │
               ├─────────────────┤
               │   BNKBDOS.SPR   │
               ├─────────────────┤
               │   BNKXDOS.SPR   │
               ├─────────────────┤
               │   TMP.SPR       │  ← Last SPR loaded
               ├─────────────────┤
               │   LCKLSTS.DAT   │  (lock table space)
               ├─────────────────┤
               │   CONSOLE.DAT   │
cur_base       └─────────────────┘
```

## MPM.SYS File Format

| Records | Content |
|---------|---------|
| 0-1 | SYSTEM.DAT (256 bytes) |
| 2 to N | System code (RESBDOS, XDOS, etc.) loaded downward from mem_top |

The file is written in this order:
1. `write$preamble` - Zeros entire file area
2. `Fx$Wr` calls - Write each SPR's code
3. `write$system$dat` - Patch records 0-1 with final SYSTEM.DAT

## SYSTEM.DAT Updates

GENSYS patches these addresses into the SPR code during linking:

| Variable | Purpose |
|----------|---------|
| `resbdos009` | RESBDOS bytes 9-11 → XDOS entry |
| `resbdos012` | RESBDOS bytes 12-17 → XIOS common base |
| `xdos003` | XDOS bytes 3-8 → XIOS common base |
| `sysdatadr` | System data page address (mem_top) |

These cross-references link the modules together.

## Automatic Mode

If invoked with `GENSYS $A`, runs in automatic mode:
- Uses default values for all prompts
- `$AR` also auto-selects all RSPs

## Error Handling

Errors cause:
1. Print error message
2. Call `system$reset` (BDOS function 0)

Common errors:
- "Disk read error" / "Disk write error"
- "Directory full"
- "File cannot fit into GENSYS buffer"
- "Memory conflict - cannot trim segment"
- "XIOS common base below the actual common base"

## Why MPM.SYS Might Be Incomplete

The actual file writing depends on:

1. **Ld$Rl** - External procedure that loads SPR files into memory buffer
2. **Fx$Wr** - External procedure that writes relocated code to file

If the CP/M emulator (cpmemu) doesn't properly implement:
- Sequential file writes (BDOS 21)
- Random file writes (BDOS 34)
- File close with buffer flush (BDOS 16)

Then the output file may be truncated or incomplete.

### Debugging Steps

1. Check if `Fx$Wr` is being called for each SPR
2. Verify BDOS function 21 writes are completing
3. Check if file close flushes all pending writes
4. Compare nmb$records in SYSTEM.DAT with actual file records

## SPR File Requirements

GENSYS expects these SPR files in the current directory:

| File | Required | Purpose |
|------|----------|---------|
| RESBDOS.SPR | Yes | Resident BDOS |
| XDOS.SPR | Yes | Extended DOS |
| BNKXIOS.SPR | Yes (banked) | Banked XIOS |
| RESXIOS.SPR | Yes (non-banked) | Resident XIOS |
| BNKBDOS.SPR | Yes | Banked BDOS file manager |
| BNKXDOS.SPR | Yes | Banked XDOS |
| TMP.SPR | If consoles > 0 | Terminal Message Processor |
| *.RSP | Optional | Resident System Processes |

## References

- MPMLDR.PLM - The loader that reads MPM.SYS
- SYSDAT.LIT - SYSTEM.DAT structure definitions
- Digital Research MP/M II System Guide
