// mkmpm.cpp - Construct complete MPM.SYS from header and SPR files
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Usage: mkmpm header.sys output.sys
//
// Reads the header from a GENSYS-generated MPM.SYS and constructs
// a complete MPM.SYS by loading each SPR file to its correct offset.

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <map>
#include <iomanip>

// Read SPR file and extract code/relocation data
// MP/M SPR file format:
//   Byte 0: 0 (indicates relocatable)
//   Byte 1: Original base page (0 = page-relative, else absolute base)
//   Bytes 2-3: Code size in 256-byte pages (little-endian)
//   Bytes 4-255: Padding (zeros)
//   Bytes 256 to 256+size-1: Code
//   Bytes 256+size onwards: Relocation bitmap (1 bit per code byte)
bool read_spr(const std::string& filename, std::vector<uint8_t>& code, uint16_t load_addr) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Warning: Cannot open " << filename << "\n";
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::cout << "    [OPEN] " << filename << " size=" << file_size << "\n";

    if (file_size < 256) {
        std::cerr << "Warning: " << filename << " too small\n";
        return false;
    }

    // Read entire file
    std::vector<uint8_t> spr(file_size);
    file.read(reinterpret_cast<char*>(spr.data()), file_size);
    file.close();

    std::cout << "    [READ] read complete\n";

    // Parse 256-byte header
    // Byte 0 is marker (0 = relocatable) - not used here
    uint8_t orig_page = spr[1];
    uint16_t size_pages = spr[2] | (spr[3] << 8);
    size_t code_size = size_pages * 256;

    std::cout << "  " << filename << ": orig=0x" << std::hex << (int)orig_page
              << " pages=" << std::dec << size_pages << " (" << code_size << " bytes)";

    if (file_size < 256 + code_size) {
        std::cerr << " ERROR: file too small for declared code size\n";
        return false;
    }

    // Extract code (starts at offset 256)
    code.assign(spr.begin() + 256, spr.begin() + 256 + code_size);

    // Relocation bitmap starts after code
    size_t bitmap_offset = 256 + code_size;
    size_t bitmap_size = (file_size > bitmap_offset) ? (file_size - bitmap_offset) : 0;

    // Apply relocation
    // For orig_page == 0: use bitmap-based relocation (page-relative addressing)
    // For orig_page != 0: skip bitmap (it's often buggy in these files) and use heuristic only
    int reloc_count = 0;
    uint8_t target_page = (load_addr >> 8);

    std::cout << "    [DEBUG] code.size()=" << code.size() << " bitmap_size=" << bitmap_size
              << " bitmap_offset=" << bitmap_offset << " spr.size()=" << spr.size() << "\n";

    if (orig_page == 0x00) {
        // Page-relative module: use bitmap-based relocation
        for (size_t byte_idx = 0; byte_idx < bitmap_size && byte_idx * 8 < code_size; byte_idx++) {
            if (bitmap_offset + byte_idx >= spr.size()) {
                std::cerr << "ERROR: bitmap access out of bounds at byte_idx=" << byte_idx << "\n";
                break;
            }
            uint8_t bitmap_byte = spr[bitmap_offset + byte_idx];
            for (int bit = 0; bit < 8; bit++) {
                size_t code_idx = byte_idx * 8 + bit;
                if (code_idx >= code_size) break;

                if (bitmap_byte & (1 << bit)) {
                    if (code_idx >= code.size()) {
                        std::cerr << "ERROR: code access out of bounds at code_idx=" << code_idx << "\n";
                        break;
                    }
                    code[code_idx] += target_page;
                    reloc_count++;
                }
            }
        }
        std::cout << " target_page=0x" << std::hex << (int)target_page << std::dec << " (bitmap) relocated=" << reloc_count << "\n";
    } else {
        // Module with non-zero orig_page: skip bitmap (often buggy), use heuristic only
        std::cout << " target_page=0x" << std::hex << (int)target_page << std::dec << " (skipping buggy bitmap)\n";
    }

    // HEURISTIC FIX: For modules with non-zero orig_page, the bitmap is often buggy.
    // We scan the entire code section for JP/CALL instructions with addresses in the
    // code range and relocate them.
    int heuristic_relocs = 0;

    if (orig_page != 0x00) {
        uint8_t max_code_page = (code_size >> 8) + 1;  // Upper bound for code-relative addresses

        // Scan the entire code section, byte by byte
        // This isn't instruction-aligned, but it catches the patterns we need
        for (size_t i = 0; i + 2 < code.size(); i++) {
            uint8_t op = code[i];

            // Check for 3-byte address instructions only (JP and CALL variants)
            // We skip LD (nn),A etc. to avoid false positives in data sections
            bool is_jp_call = (op == 0xC3 || op == 0xCD ||  // JP nn, CALL nn
                               (op & 0xC7) == 0xC2 ||       // JP cc,nn
                               (op & 0xC7) == 0xC4);        // CALL cc,nn

            if (is_jp_call) {
                // Address is in bytes i+1 (low) and i+2 (high)
                uint8_t high_byte = code[i + 2];
                // If high byte is within code range (0 to max_code_page), it's likely
                // a code-relative address that needs relocation
                if (high_byte < max_code_page) {
                    code[i + 2] = high_byte + target_page;
                    heuristic_relocs++;
                    i += 2;  // Skip past this instruction
                }
            }
        }
    }

    if (heuristic_relocs > 0) {
        std::cout << "    [HEURISTIC] Fixed " << heuristic_relocs << " JP/CALL address relocations\n";
    }

    // Debug: show first few bytes after relocation
    std::cout << "    First bytes: ";
    for (size_t i = 0; i < 16 && i < code.size(); i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)code[i] << " ";
    }
    std::cout << std::dec << "\n";

    return true;
}

// Try to find and read SPR from multiple directories
bool find_and_read_spr(const std::string& name, const std::vector<std::string>& dirs,
                       std::vector<uint8_t>& code, uint16_t load_addr) {
    for (const auto& dir : dirs) {
        std::string path = dir + "/" + name;
        std::cout << "    Trying: " << path << std::endl << std::flush;
        if (read_spr(path, code, load_addr)) {
            std::cout << "    Success, code.size()=" << code.size() << std::endl << std::flush;
            return true;
        }
    }
    std::cout << "    Not found in any directory\n";
    return false;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <header.sys> <output.sys> [spr_dir1] [spr_dir2] ...\n";
        std::cerr << "\n";
        std::cerr << "Constructs complete MPM.SYS from GENSYS header and SPR files.\n";
        return 1;
    }

    std::string header_file = argv[1];
    std::string output_file = argv[2];
    std::vector<std::string> spr_dirs;
    for (int i = 3; i < argc; i++) {
        spr_dirs.push_back(argv[i]);
    }
    if (spr_dirs.empty()) {
        spr_dirs.push_back(".");
    }

    // Read header file (first 256 bytes)
    std::ifstream hdr(header_file, std::ios::binary);
    if (!hdr) {
        std::cerr << "Error: Cannot open " << header_file << "\n";
        return 1;
    }

    uint8_t header[256];
    hdr.read(reinterpret_cast<char*>(header), 256);
    hdr.close();

    // Parse header - see sysdat.lit for offsets
    uint8_t mem_top = header[0];
    uint8_t nmb_cns = header[1];
    uint8_t brkpt_rst = header[2];
    bool bank_switched = header[4] != 0;
    uint8_t xios_base = header[7];
    uint8_t resbdos_base = header[8];
    uint8_t xdos_base = header[11];
    uint8_t rsp_base = header[12];
    uint8_t bnkxios_base = header[13];
    uint8_t bnkbdos_base = header[14];
    uint8_t bnkxdos_base = header[242];
    uint8_t tmp_base = header[247];
    uint16_t nmb_records = header[120] | (header[121] << 8);

    std::cout << "Header analysis:\n";
    std::cout << "  mem_top: 0x" << std::hex << (int)mem_top << "00\n";
    std::cout << "  nmb_cns: " << std::dec << (int)nmb_cns << "\n";
    std::cout << "  bank_switched: " << (bank_switched ? "Y" : "N") << "\n";
    std::cout << "  xios_jmp_tbl_base: 0x" << std::hex << (int)xios_base << "00\n";
    std::cout << "  resbdos_base: 0x" << (int)resbdos_base << "00\n";
    std::cout << "  xdos_base: 0x" << (int)xdos_base << "00\n";
    std::cout << "  bnkxios_base: 0x" << (int)bnkxios_base << "00\n";
    std::cout << "  bnkbdos_base: 0x" << (int)bnkbdos_base << "00\n";
    std::cout << "  bnkxdos_base: 0x" << (int)bnkxdos_base << "00\n";
    std::cout << "  tmp_base: 0x" << (int)tmp_base << "00\n";
    std::cout << "  nmb_records: " << std::dec << nmb_records << "\n";
    std::cout << "\n";

    // Calculate system size (from lowest module to mem_top)
    // For non-banked: System runs from console_dat to mem_top * 256 + 256 - 1
    uint32_t sys_top = ((uint32_t)mem_top << 8) + 256;  // Top of SYSTEM.DAT area (use 32-bit to avoid overflow)

    // Find the lowest base address
    uint16_t sys_base = 0xFFFF;
    if (resbdos_base > 0 && (resbdos_base << 8) < sys_base) sys_base = resbdos_base << 8;
    if (xdos_base > 0 && (xdos_base << 8) < sys_base) sys_base = xdos_base << 8;
    if (bnkxios_base > 0 && (bnkxios_base << 8) < sys_base) sys_base = bnkxios_base << 8;
    if (bnkbdos_base > 0 && (bnkbdos_base << 8) < sys_base) sys_base = bnkbdos_base << 8;
    if (bnkxdos_base > 0 && (bnkxdos_base << 8) < sys_base) sys_base = bnkxdos_base << 8;
    if (tmp_base > 0 && (tmp_base << 8) < sys_base) sys_base = tmp_base << 8;

    // Actually, we need to look at the GENSYS output to know the exact base
    // The console_dat_base at offset 244 tells us
    uint8_t console_dat_base = header[244];
    if (console_dat_base > 0) {
        sys_base = console_dat_base << 8;
    }

    uint32_t sys_size = sys_top - sys_base;
    std::cout << "System from 0x" << std::hex << sys_base << " to 0x" << sys_top << std::dec;
    std::cout << " = " << sys_size << " bytes\n\n";

    // Create memory image
    std::vector<uint8_t> memory(65536, 0);

    // Copy header to SYSTEM.DAT area
    memcpy(&memory[mem_top << 8], header, 256);

    // Load SPR files
    std::cout << "Loading SPR files:\n";

    // Helper lambda for safe memcpy
    auto safe_memcpy = [&memory](const char* name, uint16_t addr, const uint8_t* src, size_t size) {
        std::cout << "    [COPY] " << name << " at 0x" << std::hex << addr
                  << " size=" << std::dec << size << std::endl << std::flush;
        if (addr + size > memory.size()) {
            std::cerr << "ERROR: copy would overflow memory!\n";
            return;
        }
        memcpy(&memory[addr], src, size);
        std::cout << "    [DONE] " << name << std::endl << std::flush;
    };

    // RESBDOS
    std::vector<uint8_t> code;
    if (find_and_read_spr("RESBDOS.SPR", spr_dirs, code, resbdos_base << 8)) {
        safe_memcpy("RESBDOS", resbdos_base << 8, code.data(), code.size());
    }

    // XDOS
    if (find_and_read_spr("XDOS.SPR", spr_dirs, code, xdos_base << 8)) {
        safe_memcpy("XDOS", xdos_base << 8, code.data(), code.size());
    }

    // BNKXIOS or RESXIOS
    std::string xios_name = bank_switched ? "BNKXIOS.SPR" : "RESXIOS.SPR";
    if (find_and_read_spr(xios_name, spr_dirs, code, bnkxios_base << 8)) {
        safe_memcpy("XIOS", bnkxios_base << 8, code.data(), code.size());
    }

    // BNKBDOS
    if (find_and_read_spr("BNKBDOS.SPR", spr_dirs, code, bnkbdos_base << 8)) {
        safe_memcpy("BNKBDOS", bnkbdos_base << 8, code.data(), code.size());
    }

    // BNKXDOS
    if (find_and_read_spr("BNKXDOS.SPR", spr_dirs, code, bnkxdos_base << 8)) {
        safe_memcpy("BNKXDOS", bnkxdos_base << 8, code.data(), code.size());
    }

    // TMP
    if (find_and_read_spr("TMP.SPR", spr_dirs, code, tmp_base << 8)) {
        safe_memcpy("TMP", tmp_base << 8, code.data(), code.size());
    }

    std::cout << "\n[DEBUG] All SPR files loaded\n" << std::flush;

    // Calculate actual records needed
    // Header = 256 bytes (2 records)
    // System data from sys_base to header_addr (not including header)
    uint16_t header_addr = mem_top << 8;
    size_t system_data_size = header_addr - sys_base;  // 0xFF00 - 0x9D00 = 0x6200
    size_t actual_size = 256 + system_data_size;
    size_t actual_records = (actual_size + 127) / 128;

    std::cout << "[DEBUG] actual_size=" << actual_size << " actual_records=" << actual_records << "\n" << std::flush;

    // Update nmb_records in header
    std::cout << "[DEBUG] updating header at 0x" << std::hex << ((mem_top << 8) + 120) << std::dec << "\n" << std::flush;
    memory[(mem_top << 8) + 120] = actual_records & 0xFF;
    memory[(mem_top << 8) + 121] = (actual_records >> 8) & 0xFF;
    std::cout << "[DEBUG] header updated\n" << std::flush;

    std::cout << "\nActual records: " << actual_records << "\n";

    // Write output file
    // MPM.SYS format: Header FIRST, then system data in reverse address order
    // MPMLDR loads records to addresses descending from header address:
    //   Record 0 -> header_addr + 128 (0xFF80)
    //   Record 1 -> header_addr (0xFF00)
    //   Record 2 -> header_addr - 128 (0xFE80)
    //   etc. down to sys_base
    std::vector<uint8_t> output;

    std::cout << "[DEBUG] Starting output creation\n" << std::flush;
    std::cout << "[DEBUG] header_addr=0x" << std::hex << header_addr << " sys_base=0x" << sys_base << std::dec << "\n" << std::flush;

    // Header FIRST (256 bytes = 2 records) - from SYSTEM.DAT area at mem_top
    // Store header in normal order (as it appears in memory at header_addr)
    if (header_addr + 256 > memory.size()) {
        std::cerr << "ERROR: header read would overflow!\n";
        return 1;
    }
    output.insert(output.end(), memory.data() + header_addr, memory.data() + header_addr + 256);
    std::cout << "[DEBUG] Header copied (256 bytes), output.size()=" << output.size() << "\n" << std::flush;

    // System data in REVERSE order (from header_addr-128 down to sys_base)
    // MPMLDR continues loading records to descending addresses
    std::cout << "[DEBUG] Copying system data from 0x" << std::hex << (header_addr - 128) << " down to 0x" << sys_base << std::dec << "\n" << std::flush;
    for (int addr = header_addr - 128; addr >= (int)sys_base; addr -= 128) {
        if (addr < 0 || (size_t)(addr + 128) > memory.size()) {
            std::cerr << "ERROR: copy at addr=0x" << std::hex << addr << " would be invalid!\n";
            break;
        }
        output.insert(output.end(), memory.data() + addr, memory.data() + addr + 128);
    }
    std::cout << "[DEBUG] System data copied, output.size()=" << output.size() << "\n" << std::flush;

    // Pad to 128-byte boundary
    while (output.size() % 128 != 0) {
        output.push_back(0);
    }

    std::cout << "Output size: " << output.size() << " bytes\n";

    std::ofstream out(output_file, std::ios::binary);
    if (!out) {
        std::cerr << "Error: Cannot create " << output_file << "\n";
        return 1;
    }

    out.write(reinterpret_cast<char*>(output.data()), output.size());
    out.close();

    std::cout << "Created " << output_file << "\n";

    return 0;
}
