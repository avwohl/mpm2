"""DRI's way of making a relocatable program without a linker.

MP/M II's assembler (ASM.PRL) and debugger (DDT.COM, RDT.PRL) are MAC
sources, and Digital Research never linked them.  UTIL1/ASM.SUB and
UTIL1/DDT.SUB assemble each module twice with MAC, the second time with
the +R toggle (`mac ddt1asm $pzsz+r'), which assembles it 100H higher:
every ORG is 100H more, and so is every address derived from one.  PIP
concatenates the two HEX files and GENMOD compares the copies: a byte
that is one more in the second is the high byte of an address, and gets
a bit in the relocation map.

This module does what those tools did, for the build (tools/build.py):

    rel_bytes     the bytes an `um80 --aseg' .REL loads - what MAC puts in
                  its HEX file, a DS or ORG gap loading nothing
    mac_plus_r    a source with every ORG 100H higher, which um80 then
                  assembles the way MAC +R does
    genmod        UTIL3/GENMOD.ASM: two copies in, a .PRL out
    genhex        UTIL3/GENHEX.ASM: a file back to HEX records at an offset
    prlcom        UTIL5/PRLCM.PLM: a .PRL's image as a .COM

Each is a copy of the DRI program's logic, down to the memory GENMOD
works in: it builds the image at 0700H, after its own code and the
header page, and a byte that no HEX record loads - a DS area, or the gap
before a module's ORG - keeps whatever was in memory there.  genmod()
takes that memory as an argument.
"""

import re

# GENMOD.ASM: RMOD, the header page it writes first, and RWORK, where the
# first copy is loaded and the relocation bits are built after it.
RMOD = 0x0600
RWORK = 0x0700


class GenmodError(Exception):
    pass


# ---------------------------------------------------------------------------
# Input: the bytes an absolute .REL file loads
# ---------------------------------------------------------------------------

class _Bits:
    """The Microsoft REL format is a bit stream, most significant bit first."""

    def __init__(self, data):
        self.data = data
        self.pos = 0          # in bits

    def bits(self, n):
        v = 0
        for _ in range(n):
            byte = self.pos >> 3
            if byte >= len(self.data):
                raise GenmodError("the .REL file ends in the middle of an item")
            v = (v << 1) | ((self.data[byte] >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def word(self):
        lo = self.bits(8)
        return lo | (self.bits(8) << 8)

    def b_field(self):
        n = self.bits(3)
        if n == 0:
            first = self.bits(8)
            if first == 0xFF:           # um80's extended form
                n = self.bits(8)
                return bytes(self.bits(8) for _ in range(n))
            return bytes([first] + [self.bits(8) for _ in range(7)])
        return bytes(self.bits(8) for _ in range(n))


def rel_bytes(path):
    """The bytes a .REL file of absolute code loads: [(address, byte), ...].

    In the order they are loaded, which for one module is the order of the
    source, as in MAC's HEX file.  DS and ORG move the location counter and
    load nothing, as in MAC's HEX file: those addresses are simply missing.
    Only what `um80 --aseg' writes for a MAC source is accepted - absolute
    bytes and location counters - and anything relocatable or external
    stops the build, since GENMOD could not have seen it either.
    """
    rel = open(path, "rb").read()
    s = _Bits(rel)
    loc = 0
    out = []
    while True:
        if s.bits(1) == 0:
            out.append((loc, s.bits(8)))
            loc = (loc + 1) & 0xFFFF
            continue
        kind = s.bits(2)
        if kind != 0:
            raise GenmodError(f"{path}: relocatable word at {loc:04X}H: "
                              "not an absolute (--aseg) module")
        control = s.bits(4)
        if control in (0, 2):                   # entry symbol, program name
            s.b_field()
        elif control == 7:                      # PUBLIC
            s.bits(2)
            s.word()
            s.b_field()
        elif control in (10, 13):               # data, program size
            s.bits(2)
            s.word()
        elif control == 11:                     # set location counter
            if s.bits(2) != 0:
                raise GenmodError(f"{path}: a relocatable location counter: "
                                  "not an absolute (--aseg) module")
            loc = s.word()
        elif control in (14, 15):               # end of program, end of file
            return out
        else:
            raise GenmodError(f"{path}: link item {control} at {loc:04X}H: "
                              "only absolute code can go through GENMOD")


# ---------------------------------------------------------------------------
# MAC's +R: every ORG 100H higher
# ---------------------------------------------------------------------------

# ORG as a word of its own: MAC's names are letters, digits, `?', `@' and `$'.
_ORG = re.compile(r"(?<![\w?@$])ORG(?![\w?@$])", re.IGNORECASE)
_LABEL = re.compile(r"[ \t]*[A-Za-z_?@][\w?@$]*:?[ \t]*$")


def _statements(line):
    """A source line split the way MAC reads it.

    Returns (statements, masked, rest): the `!'-separated statements, the
    same with the inside of every quoted string blanked out, so that a
    string or comment never looks like an ORG, and the rest of the line -
    the comment and the line end.
    """
    body = line.rstrip("\r\n")
    rest = line[len(body):]
    stmts, masked, cur, mcur = [], [], [], []
    quote = False
    for i, ch in enumerate(body):
        if ch == "'":
            quote = not quote
        elif not quote and ch == ";":
            rest = body[i:] + rest
            break
        elif not quote and ch == "!":
            stmts.append("".join(cur))
            masked.append("".join(mcur))
            cur, mcur = [], []
            continue
        cur.append(ch)
        mcur.append(" " if quote and ch != "'" else ch)
    stmts.append("".join(cur))
    masked.append("".join(mcur))
    return stmts, masked, rest


def mac_plus_r(source, name="source"):
    """A MAC source assembled the way `mac x $+r' assembles it: 100H higher.

    +R is what the MP/M II Programmer's Guide (section 4.4.1) tells a user
    to do by hand to make a PRL - "assemble the source program twice,
    adding 100H to each ORG statement during the second assembly" - so
    that is what this does, to the text.  MAC.COM (run under cpmemu)
    does exactly that, `ORG $+10H' included, which comes out 200H past
    the first assembly's; and code before the first ORG starts at 100H
    rather than 0, which the ORG put in front of the source does.  The
    HEX files of all ten UTIL1 modules come out the same either way.

    Every ORG statement is shifted, including one in column 1, one after
    a `!' and one whose operand follows it with no space (`ORG(200H)').
    A label on an ORG line is refused, because um80 does not assemble it
    as MAC does, in either copy: MAC sets the label to the new location
    and um80 to the old.
    """
    newline = "\r\n" if "\r\n" in source else "\n"
    out = ["\tORG\t100H" + newline]
    # Lines end at LF only: str.splitlines() would also split at the form
    # feeds DRI's sources have, and throw the line numbers off.
    lines = [line + "\n" for line in source.split("\n")]
    lines[-1] = lines[-1][:-1]
    for number, line in enumerate(lines, 1):
        stmts, masked, rest = _statements(line)
        changed = False
        for k, (text, mask) in enumerate(zip(stmts, masked)):
            m = _ORG.search(mask)
            if not m:
                continue
            where = f"{name} line {number}: {line.strip()!r}"
            head = mask[:m.start()]
            if _LABEL.match(head) and head.strip():
                raise GenmodError(
                    f"{where}: a label on an ORG line, which MAC sets to the "
                    "new location and um80 to the old - put the label on a "
                    "line of its own after the ORG")
            if head.strip():
                raise GenmodError(f"{where}: an ORG that is not the first "
                                  "word of its statement")
            expr = text[m.end():]
            operand = expr.strip()
            if not operand:
                raise GenmodError(f"{where}: an ORG with no operand")
            lead = expr[:len(expr) - len(expr.lstrip())] or "\t"
            trail = expr[len(expr.rstrip()):]
            stmts[k] = f"{text[:m.end()]}{lead}({operand})+100H{trail}"
            changed = True
        if changed:
            line = "!".join(stmts) + rest
        out.append(line)
    return "".join(out)


# ---------------------------------------------------------------------------
# GENMOD, GENHEX, PRLCOM
# ---------------------------------------------------------------------------

def genmod(first, second, extra=0, ignore_zeros=False, memory=None):
    """UTIL3/GENMOD.ASM: make a .PRL from two copies of a program.

    `first` is the program as assembled, `second` the same assembled 100H
    higher, each a list of (address, byte) in the order the HEX records
    carry them (see rel_bytes).  `extra` is the `$nnnn' argument, the
    memory the program asks for beyond its image, and `ignore_zeros` its
    `$Z': a byte that is zero in the second copy is never compared.
    `memory` is the 64K GENMOD ran in (all zero if None): the bytes of the
    image no record loads are taken from it.

    Returns the .PRL file: the header page, the image, the relocation map,
    padded to a whole 128-byte record, just as GENMOD writes them.
    """
    mem = bytearray(memory if memory is not None else 0x10000)
    if len(mem) != 0x10000:
        raise GenmodError("GENMOD's memory must be 64K")
    if not first:
        raise GenmodError("nothing to GENMOD")

    # SETUP: the header page is cleared and the data size stored in it
    mem[RMOD:RMOD + 0x100] = bytes(0x100)
    mem[RMOD + 4] = extra & 0xFF
    mem[RMOD + 5] = (extra >> 8) & 0xFF

    # The first record fixes the bias: its address is loaded at RWORK.
    bias = (RWORK - first[0][0]) & 0xFFFF

    # Pass 0 loads the first copy and finds its top, HLOC.
    hloc = 0
    for addr, byte in first:
        m = (bias + addr) & 0xFFFF
        mem[m] = byte
        hloc = max(hloc, m)

    # The relocation map starts at HLOC+1 and is cleared up to the end of
    # the 128-byte record it ends in (LBYTE).
    def findbyte(m):
        rel = (m - RWORK) & 0xFFFF
        return (hloc + 1 + (rel >> 3)) & 0xFFFF, 0x80 >> (rel & 7)

    end, _ = findbyte(hloc + 1)
    while end & 0x7F:
        end += 1
    lbyte = end - 1
    mem[hloc + 1:lbyte + 1] = bytes(lbyte - hloc)

    # Pass 1 reads the second copy one page down and sets a bit wherever
    # it differs from the first.
    errors = []
    for addr, byte in second:
        m = (bias + addr - 0x100) & 0xFFFF
        if byte == 0 and ignore_zeros:
            continue
        diff = (byte - mem[m]) & 0xFF
        if diff == 0:
            continue
        if diff not in (1, 0xFF):
            errors.append(addr - 0x100)
        where, bit = findbyte(m)
        mem[where] |= bit
    if errors:
        raise GenmodError("RELOC ERROR AT " + " ".join(f"{a & 0xFFFF:04X}"
                                                      for a in errors[:8]))

    # TERMINATE: the module size goes in the header, and the file is the
    # header page through LBYTE.
    size = hloc - (RWORK - 1)
    mem[RMOD + 1] = size & 0xFF
    mem[RMOD + 2] = (size >> 8) & 0xFF
    return bytes(mem[RMOD:lbyte + 1])


def genhex(data, offset):
    """UTIL3/GENHEX.ASM: every byte of a file, as HEX records at `offset`."""
    return [((offset + i) & 0xFFFF, b) for i, b in enumerate(data)]


def prlcom(prl):
    """UTIL5/PRLCM.PLM: a .PRL's image, the records its header's length covers."""
    size = prl[1] | (prl[2] << 8)
    records = (size + 0x7F) >> 7
    return bytes(prl[0x100:0x100 + records * 0x80])


def prior_memory(programs):
    """The memory left by running `programs' (.COM file contents) in turn.

    Each is loaded at 0100H over whatever the one before left; nothing a
    program did while it ran is modelled.
    """
    mem = bytearray(0x10000)
    for data in programs:
        mem[0x100:0x100 + len(data)] = data
    return mem
