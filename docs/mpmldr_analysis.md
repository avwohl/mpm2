# MPMLDR Analysis

Analysis of the MP/M II loader based on source code at:
`mpm2_external/mpm2src/MPMLDR/MPMLDR.PLM`

## Files Used

- **MPM.SYS** - The complete MP/M II system (opened via BDOS function 15)
- **sysdat.lit** - Defines the 256-byte SYSTEM.DAT structure (`mpm2_external/mpm2src/UTIL8/SYSDAT.LIT`)

## MPM.SYS File Layout

| Records | Bytes | Content |
|---------|-------|---------|
| 0-1 | 0-255 | SYSTEM.DAT - configuration data |
| 2 to nmb_records-1 | 256+ | System code (XDOS, BDOS, RSPs, etc.) |

## Order of Operations

Main entry point at `start:` (line 513 of MPMLDR.PLM):

1. **Reset disk system** (BDOS function 13)
2. **Parse command tail** - can specify alternate .SYS file
3. **Print signon** - "MP/M II V2.0 Loader"
4. **Load SYSTEM.DAT** (first 256 bytes of MPM.SYS):
   - Open MPM.SYS
   - Read records 0-1 (256 bytes) into temp buffer
   - Extract `mem$top` → calculate `cur$top = mem$top * 256`
5. **Load system code** (lines 522-530):
   ```
   cur_top = mem_top * 256
   for record = 2 to nmb_records-1:
       cur_top = cur_top - 128
       read record to cur_top
   ```
   **Loads DOWNWARD from mem_top!**
6. **Set entry point**: `entry_point = xdos$base * 256`
7. **Copy SYSTEM.DAT** to `mem_top * 256`
8. **Jump to xdos_base * 256**

## Key SYSTEM.DAT Fields

The SYSTEM.DAT structure is 256 bytes. Key fields:

| Offset | Size | Field | Purpose |
|--------|------|-------|---------|
| 0 | 1 | mem_top | Top page of memory (load address / 256) |
| 1 | 1 | nmb_cns | Number of consoles |
| 2 | 1 | brkpt_RST | Breakpoint RST number |
| 3 | 1 | sys_call_stks | Add system call user stacks (boolean) |
| 4 | 1 | bank_switched | Bank switching enabled (boolean) |
| 5 | 1 | z80_cpu | Z80 CPU (boolean) |
| 6 | 1 | banked_bdos | Banked BDOS (boolean) |
| 7 | 1 | xios_jmp_tbl_base | XIOS jump table page |
| 8 | 1 | resbdos_base | Resident BDOS page |
| 9-10 | 2 | (reserved) | Used by CP/NET for master config table addr |
| 11 | 1 | **xdos_base** | **XDOS page = ENTRY POINT** |
| 12 | 1 | rsp_base | RSP base page |
| 13 | 1 | bnkxios_base | Banked XIOS page |
| 14 | 1 | bnkbdos_base | Banked BDOS page |
| 15 | 1 | nmb_mem_seg | Number of memory segments |
| 16-47 | 32 | mem_seg_tbl | 8 entries × 4 bytes (base, size, attrib, bank) |
| 48-63 | 16 | breakpoint_vector | Breakpoint vector table (filled by DDTs) |
| 64-79 | 16 | (unassigned) | |
| 80-95 | 16 | user_stacks | System call user stacks |
| 96-119 | 24 | (unassigned) | |
| 120-121 | 2 | **nmb_records** | **Total 128-byte records in MPM.SYS** |
| 122 | 1 | ticks_per_second | Timer tick rate |
| 123 | 1 | system_drive | System drive (1=A, 2=B, etc.) |
| 124 | 1 | common_base | Common memory base page |
| 125 | 1 | nmb_rsps | Number of RSPs |
| 126-127 | 2 | listcp_adr | List copy address |
| 128-143 | 16 | submit_flags | Submit flag array |
| 144-180 | 37 | copyright | Copyright message |
| 181-186 | 6 | serial_number | Serial number |
| 187 | 1 | max_locked_records | Max locked records per process |
| 188 | 1 | max_open_files | Max open files per process |
| 189-190 | 2 | total_list_items | Total list items |
| 191-192 | 2 | lock_free_space_adr | Pointer to lock table free space |
| 193 | 1 | total_system_locked_records | Total system locked records |
| 194 | 1 | total_system_open_files | Total system open files |
| 195 | 1 | day_file | Dayfile logging |
| 196 | 1 | temp_file_drive | Temporary file drive |
| 197 | 1 | nmb_printers | Number of printers |
| 198-240 | 43 | (unassigned) | |
| 241 | 1 | cmnxdos_base | Common XDOS base |
| 242 | 1 | bnkxdos_base | Banked XDOS base |
| 243 | 1 | tmpd_base | Temp PD base |
| 244 | 1 | console_dat_base | Console data base |
| 245-246 | 2 | bdos_xdos_adr | BDOS/XDOS address |
| 247 | 1 | tmp_base | TMP base address |
| 248 | 1 | nmb_brsps | Number of banked RSPs |
| 249 | 1 | brsp_base | Banked RSP base address |
| 250-251 | 2 | brspl | Non-resident RSP process link |
| 252-253 | 2 | sysdatadr | MP/M data page address |
| 254-255 | 2 | rspl | Resident system process link |

## Memory Segment Table Entry Format

Each of the 8 memory segment table entries (at offsets 16-47) is 4 bytes:

| Offset | Field | Purpose |
|--------|-------|---------|
| 0 | base | Base page of segment |
| 1 | size | Size in pages |
| 2 | attrib | Attributes |
| 3 | bank | Bank number |

## Memory Layout After Loading

```
mem_top*256+255  ┌─────────────────┐
                 │   SYSTEM.DAT    │  (256 bytes - config)
mem_top*256      ├─────────────────┤
                 │   Record 2      │  (first code record)
                 ├─────────────────┤
                 │   Record 3      │
                 │       ...       │
                 │   Record N-1    │  (last code record)
                 └─────────────────┘

Entry point: xdos_base * 256
```

## Bank Switching

MPMLDR performs **no bank switching** - everything loads into bank 0 / common memory (above common_base page). The mem_seg_tbl defines user memory segments in different banks, but those are managed by the running OS after boot.

## To Load MPM.SYS Directly in Emulator

```cpp
// 1. Read first 256 bytes of MPM.SYS
uint8_t sysdat[256];
fread(sysdat, 1, 256, fp);

// 2. Extract key values
uint8_t mem_top = sysdat[0];
uint16_t nmb_records = sysdat[120] | (sysdat[121] << 8);
uint8_t xdos_base = sysdat[11];

// 3. Load remaining records DOWNWARD from mem_top
uint16_t load_addr = mem_top * 256;
for (int rec = 2; rec < nmb_records; rec++) {
    load_addr -= 128;
    fread(&memory[load_addr], 1, 128, fp);
}

// 4. Copy SYSTEM.DAT to mem_top*256
memcpy(&memory[mem_top * 256], sysdat, 256);

// 5. Set PC to entry point
z80.pc = xdos_base * 256;

// 6. Set SP (MPMLDR sets stack to address of entry_point variable)
// You may need to set SP to a reasonable value in high memory
```

## Notes

- The loader reads MPM.SYS sequentially using BDOS function 20 (read sequential)
- Serial number matching is performed between loader and loaded system
- The `$B` command tail option enables breakpoint mode for debugging
- An alternate .SYS filename can be specified on the command line
