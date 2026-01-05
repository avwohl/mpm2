#!/usr/bin/env python3
"""
MP/M II Source Build Script

Builds MP/M II from source using:
- um80: Assembler (.ASM/.MAC -> .REL)
- ul80: Linker (.REL -> .COM/.PRL)
- cpmemu + PLM80: PL/M compiler (.PLM -> .REL) [via CP/M emulator]
- GENMOD: Creates relocatable files (.PRL/.RSP/.SPR)

Usage:
    python build.py [--clean] [--verbose] [target...]
"""

import os
import sys
import subprocess
import shutil
import argparse
from pathlib import Path
from dataclasses import dataclass
from typing import Optional

# ============================================================================
# Configuration
# ============================================================================

# Tool paths
UM80 = "um80"
UL80 = "ul80"
UPLM80 = "uplm80"
CPMEMU = os.environ.get("CPMEMU", os.path.expanduser("~/src/cpmemu/src/cpmemu"))
CPM_DISK_PY = os.environ.get("CPM_DISK", os.path.expanduser("~/src/cpmemu/util/cpm_disk.py"))

# Project root (mpm2/)
PROJECT_ROOT = Path(__file__).parent.parent

# Source directories
SRC_ROOT = PROJECT_ROOT / "mpm2_external" / "mpm2src"
LOCAL_SRC_ROOT = PROJECT_ROOT / "src" / "overrides"  # Local source overrides
BUILD_DIR = PROJECT_ROOT / "build" / "src"  # Separate from C++ build
OUTPUT_DIR = PROJECT_ROOT / "bin" / "src"   # Source-built binaries

# Runtime library for PL/M programs
CPM_RUNTIME_SRC = PROJECT_ROOT / "src" / "cpm_runtime.mac"
CPM_RUNTIME_REL = BUILD_DIR / "cpm_runtime.rel"

# Include paths for assembler
INCLUDE_PATHS = [
    SRC_ROOT / "UTIL8",  # .LIT include files
    SRC_ROOT / "NUCLEUS",
]

# ============================================================================
# Build Target Definitions
# ============================================================================

@dataclass
class BuildTarget:
    """Defines a build target"""
    name: str                    # Output filename (without extension)
    output_type: str             # 'com', 'prl', 'spr', 'rsp'
    sources: list                # List of source files (relative to SRC_ROOT)
    directory: str               # Source directory under SRC_ROOT
    origin: Optional[str] = None # Link origin (hex), None for default 0x100
    concat: bool = False         # If True, concatenate sources before assembly
    plm_mode: str = "cpm"        # PLM mode: "cpm" (default) or "bare"
    skip_runtime: bool = False   # If True, don't link with cpm_runtime
    post_build: Optional[str] = None  # Special post-build action (e.g., "mpmldr")

# Source files that produce differently-named binaries
NAME_MAPPING = {
    "SUB.PLM": "SUBMIT",
    "PRINT.PLM": "PRINTER",
    "MSCHD.PLM": "SCHED",
    "MSTS.PLM": "MPMSTAT",
    "MSPL.PLM": "SPOOL",
    "STPSP.PLM": "STOPSPLR",
    "DRST.PLM": "DSKRESET",
    "CNS.PLM": "CONSOLE",
    "PRLCM.PLM": "PRLCOM",
    "TMPSUB.ASM": "TMP",
}

# ============================================================================
# UTIL4 - File Utilities (all PL/M, all .PRL output)
# ============================================================================
UTIL4_TARGETS = [
    BuildTarget("DIR", "prl", ["DIR.PLM"], "UTIL4"),
    BuildTarget("ERA", "prl", ["ERA.PLM"], "UTIL4"),
    BuildTarget("ERAQ", "prl", ["ERAQ.PLM"], "UTIL4"),
    BuildTarget("REN", "prl", ["REN.PLM"], "UTIL4"),
    BuildTarget("TYPE", "prl", ["TYPE.PLM"], "UTIL4"),
    BuildTarget("STAT", "prl", ["STAT.PLM"], "UTIL4"),
    BuildTarget("SET", "prl", ["SET.PLM"], "UTIL4"),
    BuildTarget("SHOW", "prl", ["SHOW.PLM"], "UTIL4"),
]

# ============================================================================
# UTIL5 - System Utilities (mixed PL/M and ASM)
# ============================================================================
UTIL5_TARGETS = [
    BuildTarget("ABORT", "prl", ["ABORT.PLM"], "UTIL5"),
    BuildTarget("TOD", "prl", ["TOD.PLM"], "UTIL5"),
    BuildTarget("SUBMIT", "prl", ["SUB.PLM"], "UTIL5"),          # SUB.PLM -> SUBMIT.PRL
    BuildTarget("PRINTER", "prl", ["PRINT.PLM"], "UTIL5"),       # PRINT.PLM -> PRINTER.PRL
    BuildTarget("SCHED", "prl", ["MSCHD.PLM"], "UTIL5"),         # MSCHD.PLM -> SCHED.PRL
    BuildTarget("MPMSTAT", "prl", ["MSTS.PLM"], "UTIL5"),        # MSTS.PLM -> MPMSTAT.PRL
    BuildTarget("SPOOL", "prl", ["MSPL.PLM"], "UTIL5"),          # MSPL.PLM -> SPOOL.PRL
    BuildTarget("STOPSPLR", "prl", ["STPSP.PLM"], "UTIL5"),      # STPSP.PLM -> STOPSPLR.PRL
    BuildTarget("DSKRESET", "prl", ["DRST.PLM"], "UTIL5"),       # DRST.PLM -> DSKRESET.PRL
    BuildTarget("CONSOLE", "prl", ["CNS.PLM"], "UTIL5"),         # CNS.PLM -> CONSOLE.PRL
    BuildTarget("USER", "prl", ["USER.PLM"], "UTIL5"),
    BuildTarget("PRLCOM", "prl", ["PRLCM.PLM"], "UTIL5"),        # PRLCM.PLM -> PRLCOM.PRL
    BuildTarget("DUMP", "prl", ["DUMP.ASM"], "UTIL5"),
]

# ============================================================================
# UTIL6 - Text Processing (PL/M)
# ============================================================================
UTIL6_TARGETS = [
    BuildTarget("PIP", "prl", ["PIP.PLM"], "UTIL6"),
    BuildTarget("ED", "prl", ["ED.PLM"], "UTIL6"),
]

# ============================================================================
# UTIL3 - Code Generation Tools
# ============================================================================
UTIL3_TARGETS = [
    BuildTarget("LOAD", "prl", ["LOAD.PLM"], "UTIL3"),
    BuildTarget("GENHEX", "com", ["GENHEX.ASM"], "UTIL3"),
    BuildTarget("GENMOD", "com", ["GENMOD.ASM"], "UTIL3"),
]

# ============================================================================
# UTIL1 - Assembler & Debugger (multi-module ASM)
# ============================================================================
UTIL1_TARGETS = [
    BuildTarget("ASM", "prl", [
        "AS0COM.ASM", "AS1IO.ASM", "AS2SCAN.ASM",
        "AS3SYM.ASM", "AS4SEAR.ASM", "AS5OPER.ASM", "AS6MAIN.ASM"
    ], "UTIL1"),
    BuildTarget("RDT", "prl", [
        "DDT0MOV.ASM", "DDT1ASM.ASM", "DDT2MON.ASM"
    ], "UTIL1"),
    BuildTarget("DDT", "com", [
        "DDT0MOV.ASM", "DDT1ASM.ASM", "DDT2MON.ASM"
    ], "UTIL1"),
]

# ============================================================================
# UTIL7 - SDIR (Super Directory - multi-module PL/M)
# ============================================================================
UTIL7_TARGETS = [
    BuildTarget("SDIR", "prl", [
        "DM.PLM", "SN.PLM", "DSE.PLM", "DSH.PLM",
        "DSO.PLM", "DA.PLM", "DP.PLM", "DTS.PLM"
    ], "UTIL7"),
]

# ============================================================================
# MPMLDR - System Loader
# ============================================================================
MPMLDR_TARGETS = [
    # MPMLDR needs special handling:
    # - Uses --mode bare for proper stack initialization
    # - Uses modified LDMONX.ASM wrapper for BDOS calls
    # - Doesn't link with cpm_runtime
    # - Post-build combines with LDRBDOS binary
    BuildTarget("MPMLDR", "com", ["MPMLDR.PLM", "LDMONX.ASM"], "MPMLDR",
                plm_mode="bare", skip_runtime=True, post_build="mpmldr"),
    # GENSYS needs LDRLWR.ASM for LDRL/FXWR and X0100.ASM for standard symbols
    BuildTarget("GENSYS", "com", ["GENSYS.PLM", "LDRLWR.ASM", "X0100.ASM"], "MPMLDR"),
]

# Path to LDRBDOS binary (extracted from DRI MPMLDR.COM)
LDRBDOS_BIN = BUILD_DIR / "MPMLDR" / "ldrbdos.bin"
DRI_MPMLDR = SRC_ROOT / "MPMLDR" / "MPMLDR.COM"

# ============================================================================
# NUCLEUS - Kernel SPR files
# ============================================================================
# XDOS is built from many modules (from XDOS1.SUB):
# xdos1.lib = xdos.rel, dsptch.rel, queue.rel, flag.rel, memmgr.rel, th.rel, lst.rel
# xdos2.lib = cli.rel, tick.rel, clock.rel, attach.rel
# xdos3.lib = datapg.rel, mpm.rel, rlsmx.rel, rlsdev.rel
# Final: ver.rel, xdos3.lib, xdos2.lib, xdos1.lib, patch.rel, clbdos.rel, xdosif.rel
XDOS_MODULES = [
    "VER.ASM",      # Version info
    "DATAPG.ASM",   # Data page (provides nmblst, etc.)
    "MPM.ASM",      # Main MP/M code
    "RLSMX.ASM",    # Release mutex
    "RLSDEV.ASM",   # Release device
    "CLI.ASM",      # Command line interface
    "TICK.ASM",     # Tick handler
    "CLOCK.ASM",    # Clock
    "ATTACH.ASM",   # Attach
    "XDOS.ASM",     # XDOS entry
    "DSPTCH.ASM",   # Dispatcher
    "QUEUE.ASM",    # Queue management
    "FLAG.ASM",     # Flag management
    "MEMMGR.ASM",   # Memory manager
    "TH.ASM",       # Terminal handler
    "LST.ASM",      # List handler
    "PATCH.ASM",    # Patches
    "CLBDOS.ASM",   # CL BDOS
    "XDOSIF.ASM",   # XDOS interface
]

NUCLEUS_TARGETS = [
    BuildTarget("XDOS", "spr", XDOS_MODULES, "NUCLEUS"),
    BuildTarget("BNKXDOS", "spr", ["BNKXDOS.ASM"], "NUCLEUS"),
    # RESBDOS is built from RESBDOS1.ASM + CONBDOS.ASM concatenated
    # (RESBDOS1 defines symbols that CONBDOS references)
    BuildTarget("RESBDOS", "spr", ["RESBDOS1.ASM", "CONBDOS.ASM"], "NUCLEUS", concat=True),
    BuildTarget("TMP", "spr", ["TMPSUB.ASM"], "NUCLEUS", skip_runtime=True),
]

# ============================================================================
# BNKBDOS - Banked BDOS
# ============================================================================
BNKBDOS_TARGETS = [
    BuildTarget("BNKBDOS", "spr", ["BNKBDOS.ASM"], "BNKBDOS"),
]

# ============================================================================
# UTIL2 - RSP Modules
# ============================================================================
UTIL2_RSP_TARGETS = [
    # MSCMN.PLM is $INCLUDE'd by MSBRS.PLM, not compiled separately
    BuildTarget("MPMSTAT", "rsp", ["MSBRS.PLM", "MSRSP.PLM"], "UTIL2"),
    BuildTarget("SCHED", "rsp", ["SCBRS.PLM", "SCRSP.PLM"], "UTIL2"),
    BuildTarget("SPOOL", "rsp", ["SPBRS.PLM", "SPRSP.PLM"], "UTIL2"),
    BuildTarget("ABORT", "rsp", ["ABORT.ASM"], "UTIL2"),
]

# All targets
ALL_TARGETS = (
    UTIL4_TARGETS +
    UTIL5_TARGETS +
    UTIL6_TARGETS +
    UTIL3_TARGETS +
    UTIL1_TARGETS +
    UTIL7_TARGETS +
    MPMLDR_TARGETS +
    NUCLEUS_TARGETS +
    BNKBDOS_TARGETS +
    UTIL2_RSP_TARGETS
)

# ============================================================================
# Build Functions
# ============================================================================

class Builder:
    def __init__(self, verbose=False):
        self.verbose = verbose
        self.build_dir = BUILD_DIR
        self.output_dir = OUTPUT_DIR

    def log(self, msg):
        print(msg)

    def debug(self, msg):
        if self.verbose:
            print(f"  {msg}")

    def run(self, cmd, cwd=None):
        """Run a command and return success status"""
        self.debug(f"Running: {' '.join(cmd)}")
        try:
            result = subprocess.run(
                cmd,
                cwd=cwd,
                capture_output=True,
                text=True
            )
            if result.returncode != 0:
                print(f"ERROR: {' '.join(cmd)}")
                print(result.stderr)
                return False
            if self.verbose and result.stdout:
                print(result.stdout)
            return True
        except Exception as e:
            print(f"EXCEPTION: {e}")
            return False

    def setup_dirs(self):
        """Create build directories"""
        self.build_dir.mkdir(parents=True, exist_ok=True)
        self.output_dir.mkdir(parents=True, exist_ok=True)

    def clean(self):
        """Clean build directories"""
        if self.build_dir.exists():
            shutil.rmtree(self.build_dir)
        if self.output_dir.exists():
            shutil.rmtree(self.output_dir)
        self.log("Cleaned build directories")

    def assemble(self, asm_file: Path, rel_file: Path) -> bool:
        """Assemble a .ASM file to .REL using um80"""
        cmd = [UM80]

        # Add include paths
        for inc in INCLUDE_PATHS:
            cmd.extend(["-I", str(inc)])

        cmd.extend(["-o", str(rel_file), str(asm_file)])

        return self.run(cmd)

    def compile_plm(self, plm_file: Path, rel_file: Path, mode: str = "cpm") -> bool:
        """Compile a .PLM file to .REL using uplm80 + um80

        uplm80 compiles .PLM -> .MAC (assembly)
        um80 assembles .MAC -> .REL

        Args:
            mode: "cpm" (default) or "bare" for bare-metal startup
        """
        # Generate intermediate .MAC file
        mac_file = self.build_dir / (plm_file.stem + ".MAC")

        # Step 1: Compile PLM to MAC
        # Add include path for .LIT files
        cmd = [UPLM80, "-I", str(SRC_ROOT / "UTIL8")]
        if mode == "bare":
            cmd.extend(["--mode", "bare"])
        cmd.extend(["-o", str(mac_file), str(plm_file)])
        if not self.run(cmd):
            return False

        # Step 2: Assemble MAC to REL
        return self.assemble(mac_file, rel_file)

    def link(self, rel_files: list, output_file: Path, output_type: str, origin: str = None) -> bool:
        """Link .REL files to output using ul80"""
        cmd = [UL80]

        if output_type == "prl" or output_type == "rsp" or output_type == "spr":
            cmd.append("--prl")

        if origin:
            cmd.extend(["-p", origin])

        cmd.extend(["-o", str(output_file)])
        cmd.extend([str(f) for f in rel_files])

        return self.run(cmd)

    def build_target(self, target: BuildTarget) -> bool:
        """Build a single target"""
        self.log(f"Building {target.name}.{target.output_type}...")

        # Determine source directories (local overrides take precedence)
        src_dir = SRC_ROOT / target.directory
        local_src_dir = LOCAL_SRC_ROOT / target.directory

        # Compile/assemble each source file
        rel_files = []
        all_success = True

        # Handle concatenated sources (multiple files -> single assembly)
        if target.concat and len(target.sources) > 1:
            # Concatenate all source files into one
            concat_content = []
            for src in target.sources:
                local_src_path = local_src_dir / src
                orig_src_path = src_dir / src
                if local_src_path.exists():
                    src_path = local_src_path
                    self.debug(f"Using local override: {src_path}")
                elif orig_src_path.exists():
                    src_path = orig_src_path
                else:
                    self.log(f"  ERROR: Source file not found: {orig_src_path}")
                    return False
                try:
                    content = src_path.read_text(encoding='latin-1')
                    # Strip trailing Control-Z (CP/M EOF marker) characters
                    content = content.rstrip('\x1a')
                    concat_content.append(content)
                except Exception as e:
                    self.log(f"  ERROR: Failed to read {src_path}: {e}")
                    return False

            # Write concatenated file
            concat_file = self.build_dir / f"{target.name}.ASM"
            concat_file.write_text('\n'.join(concat_content), encoding='latin-1')

            # Assemble concatenated file
            rel_path = self.build_dir / f"{target.name}.REL"
            if self.assemble(concat_file, rel_path):
                rel_files.append(rel_path)
            else:
                return False
        else:
            # Normal case: process each source file separately
            for src in target.sources:
                # Check local source directory first, then original
                local_src_path = local_src_dir / src
                orig_src_path = src_dir / src
                if local_src_path.exists():
                    src_path = local_src_path
                    self.debug(f"Using local override: {src_path}")
                elif orig_src_path.exists():
                    src_path = orig_src_path
                else:
                    self.log(f"  ERROR: Source file not found: {orig_src_path}")
                    all_success = False
                    continue

                rel_name = Path(src).stem + ".REL"
                rel_path = self.build_dir / rel_name

                if src.upper().endswith(".ASM") or src.upper().endswith(".MAC"):
                    if self.assemble(src_path, rel_path):
                        rel_files.append(rel_path)
                    else:
                        all_success = False
                elif src.upper().endswith(".PLM"):
                    if self.compile_plm(src_path, rel_path, target.plm_mode):
                        rel_files.append(rel_path)
                    else:
                        all_success = False
                else:
                    self.log(f"  ERROR: Unknown source type: {src}")
                    all_success = False

        if not rel_files:
            self.log(f"  ERROR: No object files produced for {target.name}")
            return False

        # Include the CP/M runtime library (provides standard CP/M symbols)
        # unless skip_runtime is set (e.g., for MPMLDR which has its own BDOS)
        if not target.skip_runtime:
            # Build runtime if needed
            if not CPM_RUNTIME_REL.exists():
                self.debug("Building CP/M runtime library...")
                if not self.assemble(CPM_RUNTIME_SRC, CPM_RUNTIME_REL):
                    self.log("  ERROR: Failed to build CP/M runtime")
                    return False
            rel_files.append(CPM_RUNTIME_REL)

        # Link
        output_file = self.output_dir / f"{target.name}.{target.output_type.upper()}"
        if not self.link(rel_files, output_file, target.output_type, target.origin):
            return False

        # Handle post-build actions
        if target.post_build == "mpmldr":
            if not self.post_build_mpmldr(output_file):
                return False

        self.log(f"  Created {output_file}")
        return all_success

    def post_build_mpmldr(self, output_file: Path) -> bool:
        """
        Post-build step for MPMLDR: combine with LDRBDOS binary.

        MPMLDR memory map:
        - 0x100: Main loader code (from PLM/ASM)
        - 0xD00: LDRBDOS (extracted from DRI MPMLDR.COM)
        - 0x1700: LDRBIOS (loaded at runtime)
        """
        # Create MPMLDR build directory if needed
        mpmldr_build_dir = self.build_dir / "MPMLDR"
        mpmldr_build_dir.mkdir(parents=True, exist_ok=True)

        # Extract LDRBDOS from DRI's MPMLDR.COM if not already done
        if not LDRBDOS_BIN.exists():
            self.debug("Extracting LDRBDOS from DRI MPMLDR.COM...")
            if not DRI_MPMLDR.exists():
                self.log(f"  ERROR: DRI MPMLDR.COM not found: {DRI_MPMLDR}")
                return False

            # Read DRI MPMLDR.COM and extract LDRBDOS (at offset 0xC00, length 0xA80)
            # The binary loads at 0x100, so LDRBDOS at 0xD00 is at file offset 0xC00
            try:
                with open(DRI_MPMLDR, 'rb') as f:
                    data = f.read()
                # LDRBDOS is at 0xD00 in memory, file starts at 0x100, so offset = 0xD00 - 0x100 = 0xC00
                ldrbdos_offset = 0xD00 - 0x100
                ldrbdos_size = 0xA80  # Size extracted during investigation
                ldrbdos_data = data[ldrbdos_offset:ldrbdos_offset + ldrbdos_size]
                with open(LDRBDOS_BIN, 'wb') as f:
                    f.write(ldrbdos_data)
                self.debug(f"Extracted {len(ldrbdos_data)} bytes of LDRBDOS")
            except Exception as e:
                self.log(f"  ERROR: Failed to extract LDRBDOS: {e}")
                return False

        # Use dri_patch tool to combine main code with LDRBDOS
        dri_patch = Path(__file__).parent / "dri_patch.py"
        if not dri_patch.exists():
            self.log(f"  ERROR: dri_patch tool not found: {dri_patch}")
            return False

        # Create a backup of the original linked output
        linked_output = output_file.with_suffix('.linked')
        shutil.copy(output_file, linked_output)

        # Combine: base at 0x100, LDRBDOS at 0xD00
        cmd = [
            "python3", str(dri_patch),
            "--base", f"{linked_output}@0x100",
            "--patch", f"0xD00:{LDRBDOS_BIN}",
            "-o", str(output_file)
        ]

        if not self.run(cmd):
            self.log("  ERROR: Failed to combine MPMLDR with LDRBDOS")
            return False

        self.debug("Combined MPMLDR with LDRBDOS")
        return True

    def build_all(self, targets=None):
        """Build all or specified targets"""
        self.setup_dirs()

        if targets is None:
            targets = ALL_TARGETS

        success_count = 0
        fail_count = 0
        skip_count = 0

        for target in targets:
            if self.build_target(target):
                success_count += 1
            else:
                fail_count += 1

        self.log("")
        self.log(f"Build complete: {success_count} succeeded, {fail_count} failed, {skip_count} skipped")
        return fail_count == 0

# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description="Build MP/M II from source")
    parser.add_argument("--clean", action="store_true", help="Clean build directories")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--list", action="store_true", help="List all targets")
    parser.add_argument("--asm-only", action="store_true", help="Build only ASM targets (skip PL/M)")
    parser.add_argument("targets", nargs="*", help="Specific targets to build")

    args = parser.parse_args()

    builder = Builder(verbose=args.verbose)

    if args.clean:
        builder.clean()
        return 0

    if args.list:
        print("Available targets:")
        for t in ALL_TARGETS:
            plm_flag = " [PLM]" if any(s.upper().endswith(".PLM") for s in t.sources) else ""
            print(f"  {t.name}.{t.output_type} <- {', '.join(t.sources)}{plm_flag}")
        return 0

    # Filter to ASM-only if requested
    if args.asm_only:
        targets = [t for t in ALL_TARGETS
                   if all(s.upper().endswith(".ASM") or s.upper().endswith(".MAC")
                          for s in t.sources)]
    elif args.targets:
        target_names = [t.upper() for t in args.targets]
        targets = [t for t in ALL_TARGETS if t.name.upper() in target_names]
        if not targets:
            print(f"No matching targets found for: {args.targets}")
            return 1
    else:
        targets = ALL_TARGETS

    success = builder.build_all(targets)
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())
