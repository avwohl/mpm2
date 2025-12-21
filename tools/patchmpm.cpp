// patchmpm.cpp - Patch incomplete MPM.SYS with missing modules
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Takes a GENSYS-generated MPM.SYS (which may be incomplete) and
// patches it with the correct module data to make it complete.
//
// Usage: patchmpm <input.sys> <output.sys> <spr_dir1> [spr_dir2 ...]

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

// Read raw binary SPR file (skip 256-byte header/bitmap, get code)
bool read_spr_raw(const std::string& filename, std::vector<uint8_t>& code) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();

    if (file_size <= 256) {
        std::cerr << "Warning: " << filename << " too small\n";
        return false;
    }

    // Read header to get psize
    file.seekg(0, std::ios::beg);
    uint8_t header[256];
    file.read(reinterpret_cast<char*>(header), 256);

    // psize at bytes 1-2 (little-endian)
    uint16_t psize = header[1] | (header[2] << 8);

    // Code starts at offset 256
    file.seekg(256, std::ios::beg);
    code.resize(psize);
    file.read(reinterpret_cast<char*>(code.data()), psize);

    size_t read_bytes = file.gcount();
    if (read_bytes < psize) {
        code.resize(read_bytes);
    }

    std::cout << "  " << filename << ": " << code.size() << " bytes\n";
    return true;
}

// Apply simple page relocation to code
void relocate_code(std::vector<uint8_t>& code, uint16_t load_addr) {
    // Read bitmap from SPR file header
    // For simplicity, we won't do relocation - the emulator intercepts XIOS anyway
    // The modules should work without relocation for testing
    (void)code;
    (void)load_addr;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <input.sys> <output.sys> <spr_dir1> [spr_dir2 ...]\n";
        return 1;
    }

    std::string input_file = argv[1];
    std::string output_file = argv[2];
    std::vector<std::string> spr_dirs;
    for (int i = 3; i < argc; i++) {
        spr_dirs.push_back(argv[i]);
    }

    // Read input MPM.SYS (may be incomplete)
    std::ifstream in(input_file, std::ios::binary);
    if (!in) {
        std::cerr << "Error: Cannot open " << input_file << "\n";
        return 1;
    }

    in.seekg(0, std::ios::end);
    size_t input_size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> mpm(input_size);
    in.read(reinterpret_cast<char*>(mpm.data()), input_size);
    in.close();

    std::cout << "Input: " << input_file << " (" << input_size << " bytes)\n\n";

    // Parse header
    uint8_t mem_top = mpm[0];
    uint8_t nmb_cns = mpm[1];
    uint8_t resbdos_base = mpm[8];
    uint8_t xdos_base = mpm[11];
    uint8_t bnkxios_base = mpm[13];
    uint8_t bnkbdos_base = mpm[14];
    uint8_t bnkxdos_base = mpm[242];
    uint8_t tmp_base = mpm[247];
    uint8_t console_dat_base = mpm[244];
    uint16_t nmb_records = mpm[120] | (mpm[121] << 8);

    std::cout << "Header:\n";
    std::cout << "  mem_top: 0x" << std::hex << (int)mem_top << "00\n";
    std::cout << "  nmb_cns: " << std::dec << (int)nmb_cns << "\n";
    std::cout << "  resbdos_base: 0x" << std::hex << (int)resbdos_base << "00\n";
    std::cout << "  xdos_base: 0x" << (int)xdos_base << "00\n";
    std::cout << "  bnkxios_base: 0x" << (int)bnkxios_base << "00\n";
    std::cout << "  bnkbdos_base: 0x" << (int)bnkbdos_base << "00\n";
    std::cout << "  bnkxdos_base: 0x" << (int)bnkxdos_base << "00\n";
    std::cout << "  tmp_base: 0x" << (int)tmp_base << "00\n";
    std::cout << "  console_dat_base: 0x" << (int)console_dat_base << "00\n";
    std::cout << "  nmb_records: " << std::dec << nmb_records << "\n\n";

    // Calculate expected size
    uint32_t sys_top = ((uint32_t)mem_top << 8) + 256;
    uint16_t sys_base = console_dat_base << 8;
    uint32_t sys_size = sys_top - sys_base;
    size_t expected_records = (256 + sys_size + 127) / 128;

    std::cout << "System: 0x" << std::hex << sys_base << " to 0x" << sys_top << std::dec;
    std::cout << " = " << sys_size << " bytes\n";
    std::cout << "Expected records: " << expected_records << "\n\n";

    // Create 64K+1 memory image (extra byte for when sys_top = 0x10000)
    std::vector<uint8_t> memory(65537, 0);

    // First, copy the existing MPM.SYS data
    // MPM.SYS format: first 256 bytes = header
    // Remaining data loads downward from sys_top
    memcpy(&memory[mem_top << 8], mpm.data(), 256);  // Header at FF00

    // Data records load from sys_top - 128 downward
    size_t data_offset = 256;  // Skip header
    for (uint32_t addr = sys_top - 128; addr >= sys_base && data_offset < input_size; addr -= 128) {
        size_t bytes_to_copy = std::min((size_t)128, input_size - data_offset);
        memcpy(&memory[addr], &mpm[data_offset], bytes_to_copy);
        data_offset += 128;
    }

    std::cout << "Loaded " << (data_offset - 256) << " bytes from input\n\n";

    // Now load SPR files to fill any gaps
    std::cout << "Loading SPR files:\n";

    auto find_spr = [&spr_dirs](const std::string& name, std::vector<uint8_t>& code) -> bool {
        for (const auto& dir : spr_dirs) {
            if (read_spr_raw(dir + "/" + name, code)) {
                return true;
            }
        }
        return false;
    };

    std::vector<uint8_t> code;

    // Load RESBDOS
    if (find_spr("RESBDOS.SPR", code)) {
        uint16_t addr = resbdos_base << 8;
        memcpy(&memory[addr], code.data(), code.size());
    }

    // Load XDOS
    if (find_spr("XDOS.SPR", code)) {
        uint16_t addr = xdos_base << 8;
        memcpy(&memory[addr], code.data(), code.size());
    }

    // Load BNKXIOS or RESXIOS
    if (find_spr("RESXIOS.SPR", code) || find_spr("BNKXIOS.SPR", code)) {
        uint16_t addr = bnkxios_base << 8;
        memcpy(&memory[addr], code.data(), code.size());
    }

    // Load BNKBDOS
    if (find_spr("BNKBDOS.SPR", code)) {
        uint16_t addr = bnkbdos_base << 8;
        memcpy(&memory[addr], code.data(), code.size());
    }

    // Load BNKXDOS
    if (find_spr("BNKXDOS.SPR", code)) {
        uint16_t addr = bnkxdos_base << 8;
        memcpy(&memory[addr], code.data(), code.size());
    }

    // Load TMP
    if (find_spr("TMP.SPR", code)) {
        uint16_t addr = tmp_base << 8;
        memcpy(&memory[addr], code.data(), code.size());
    }

    // Build output file
    std::vector<uint8_t> output;

    // Header (256 bytes) - update nmb_records
    memory[(mem_top << 8) + 120] = expected_records & 0xFF;
    memory[(mem_top << 8) + 121] = (expected_records >> 8) & 0xFF;
    output.insert(output.end(), &memory[mem_top << 8], &memory[(mem_top << 8) + 256]);

    // Data (from sys_top-128 down to sys_base)
    for (uint32_t addr = sys_top - 128; addr >= sys_base; addr -= 128) {
        output.insert(output.end(), &memory[addr], &memory[addr + 128]);
    }

    std::cout << "\nOutput: " << output.size() << " bytes (" << (output.size() / 128) << " records)\n";

    // Write output
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
