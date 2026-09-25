# LDRLWR.ASM and the relocation bit map in MP/M II GENSYS

**Corrected in 0.3.6: DRI's GENSYS.COM does not have this bug.**  The
write-up below was released with 0.3.4 and is kept as it was written,
but its conclusion is wrong.

`LdRl` compares only low bytes, but it never compares addresses in two
different buffers.  `btmptp` starts as the low byte of the end of the
records it has loaded into `sctbfr`, and the moment the bit map pointer
reaches that byte `LdRl` reads the next record into `bitmap`, points at
`bitmap` and sets `btmptp` to `low(bitmap+128)`.  Within either buffer
the pointer covers at most 128 consecutive bytes before it reaches that
end, so its low byte matches `btmptp` there and nowhere earlier.  When
the program length is a multiple of 128 the pointer starts at the end of
the loaded records, the first comparison matches, and the bit map is
read from the file before any of it is used - no garbage.  In DRI's
binaries both immediates are right: V2.0's `GENSYS.COM` has `MVI A,5CH`
for `low(bitmap+128)` with `bitmap` at 1DDCH, and `SUI 3DH` for
`low(sctbfr)` with `sctbfr` at 253DH.

Measured: DRI's V2.1 `GENSYS.COM`, run under cpmemu, relocates an RSP of
128, 256, 512, 1008, 1024, 1152, 1536 and 1664 bytes exactly as
`tools/gensys.py` does, and DRI's V2.0 `GENSYS.COM` one of 256, 1024 and
1536 bytes.  Each test RSP had a relocation bit set on every fifth byte
past its first 32 bytes; every one of those bytes, and no other,
came out with the RSP's base page added.

What did relocate with a wrong bit map was the `GENSYS.COM` built from
source.  um80 before 0.3.49 assembled `low(bitmap+128)` as the low byte
of `bitmap+128`'s offset within `LDRLWR`'s module, with no relocation,
so `btmptp` was wrong and the next bit map record was read too early or
too late.  DRI assembled `LDRLWR.ASM` with ASM80, whose object format
carries that relocation.  See the GENSYS section of
[mpm2_v21.md](mpm2_v21.md).

`tools/gensys.py` is still how this repository generates `MPM.SYS`,
because it runs on the host and takes its answers from JSON, not
because DRI's GENSYS is wrong.

## Original write-up (0.3.4, superseded)

### Summary

The MP/M II GENSYS.COM utility (V2.0/V2.1) contains a bug in its PRL/SPR file relocation code that corrupts files when the code size is an exact multiple of 128 bytes. The bug is most severe at exactly 1024 bytes, where 100% of the relocation bitmap is garbage data.

### Affected Software

- **GENSYS.COM** - MP/M II System Generation utility
- Specifically: **LDRLWR.ASM** (Load, Relocate, Write module linked into GENSYS)
- Source: `MPMLDR/LDRLWR.ASM` in MP/M II source distribution

### Not Affected

- **CLI.ASM** (Command Line Interpreter) - loads entire PRL file into memory before relocating
- **PRLCM.PLM** (PRL to COM converter) - doesn't perform relocation
- **GENMOD.ASM** (HEX to PRL generator) - creates bitmaps, doesn't apply them

User programs run correctly; only system generation is affected.

### The Bug

#### Background: PRL/SPR File Format

PRL (Page Relocatable) and SPR files contain:
1. Header (128 bytes) - includes code size at offset 1-2
2. Code data (`prgsiz` bytes)
3. Relocation bitmap (`ceil(prgsiz/8)` bytes) - 1 bit per code byte

During relocation, each bit in the bitmap indicates whether the corresponding code byte should have a relocation offset added.

#### What LDRLWR.ASM Does

The `LdRl` procedure (lines 26-125) loads SPR files and applies relocation:

1. **Lines 29-50**: Calculate sectors needed as `ceil(prgsiz/128)` and read them into `sctbfr`
2. **Lines 58-68**: Set bitmap pointer to `sctbfr + prgsiz` (the "extra bytes" after code from sector rounding)
3. **Lines 66-68**: Set `btmptp = low(DMA) + 128` as end-of-bitmap marker
4. **Lines 71-117**: Loop through code bytes, applying relocation offset where bitmap bits are set
5. **Lines 84-99**: When bitmap is exhausted, read more from disk

#### The Flaw

Lines 85-89 check whether to read more bitmap data from disk:

```asm
    xthl              ; HL = current bitmap pointer
    lda   btmptp      ; A = end marker (low byte only)
    cmp   l           ; Compare low bytes
    jnz   pgrel2      ; If not equal, continue using buffer
    ; ... read more bitmap from disk ...
```

**Problems:**

1. `btmptp` is initialized to `low(DMA) + 128`, where DMA points into `sctbfr`
2. The bitmap pointer walks through `sctbfr + prgsiz` initially
3. After a disk read, bitmap data goes to a separate `bitmap` buffer (line 222)
4. The comparison only checks low bytes of addresses in **different memory regions**
5. The check doesn't fire at the right time (or at all), causing garbage to be used as bitmap

#### Trigger Condition

The bug triggers when **prgsiz is an exact multiple of 128 bytes**.

When `prgsiz % 128 == 0`:
- Sectors loaded = `prgsiz / 128`
- Extra bytes available = `(prgsiz/128) * 128 - prgsiz = 0`
- Bitmap pointer starts exactly at end of loaded data
- No valid bitmap bytes are available in the loaded sectors
- The flawed check allows 128 bytes of garbage to be read before triggering a disk read

#### Severity by File Size

| prgsiz | Extra bytes | Bitmap needed | Garbage used | Corruption |
|--------|-------------|---------------|--------------|------------|
| 128    | 0           | 16 bytes      | up to 16     | up to 100% |
| 256    | 0           | 32 bytes      | up to 32     | up to 100% |
| 512    | 0           | 64 bytes      | up to 64     | up to 100% |
| **1024** | **0**     | **128 bytes** | **128**      | **100%**   |
| 2048   | 0           | 256 bytes     | 128 first    | 50%        |

**1024 bytes is the worst case**: the bitmap is exactly 128 bytes, and exactly 128 bytes of garbage are consumed before any disk read would be attempted.

Non-multiples of 128 may also be affected depending on memory address alignment, but have partial protection from the rounding bytes.

### Impact

- SPR/RSP/BRS modules with code sizes that are multiples of 128 bytes will be incorrectly relocated during GENSYS
- The resulting MPM.SYS may crash, hang, or behave incorrectly
- Symptoms are unpredictable since random memory is used as the relocation bitmap

### Workaround

Replace GENSYS.COM with an implementation that reads the complete bitmap directly from the SPR file after the code section, rather than relying on the flawed incremental loading.

### References

- Source file: `MPMLDR/LDRLWR.ASM` (MP/M II V2.0 source distribution)
- Bug is in the `LdRl` procedure, lines 26-125
- Critical flawed check: lines 85-89
