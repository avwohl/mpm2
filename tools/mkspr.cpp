// mkspr.cpp - Create SPR file from binary with relocation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Creates an SPR (System Page Relocatable) file from a raw binary.
// Scans for JP/CALL instructions and marks high bytes for relocation.
//
// SPR format:
//   Bytes 0-127: Header
//     0: unused
//     1-2: psize (program size in bytes, little-endian)
//     3: unused
//     4-5: dsize (data/buffer size, little-endian)
//     6-127: unused (zeros)
//   Bytes 128-255: Relocation bitmap (1 bit per byte of code)
//   Bytes 256+: Code

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " input.bin output.spr [bufsize]\n"
              << "\n"
              << "Creates an SPR file from a raw binary.\n"
              << "Scans for JP/CALL/LD instructions and marks for relocation.\n"
              << "\n"
              << "Arguments:\n"
              << "  input.bin   Input binary file (ORG 0)\n"
              << "  output.spr  Output SPR file\n"
              << "  bufsize     Optional buffer/data size (default: 0)\n"
              << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = argv[2];
    uint16_t bufsize = 0;

    if (argc > 3) {
        bufsize = std::stoi(argv[3]);
    }

    // Read input binary
    std::ifstream in(input_file, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input: " << input_file << "\n";
        return 1;
    }

    in.seekg(0, std::ios::end);
    size_t code_size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> code(code_size);
    in.read(reinterpret_cast<char*>(code.data()), code_size);
    in.close();

    std::cout << "Input: " << input_file << " (" << code_size << " bytes)\n";

    // Create relocation bitmap (128 bytes = 1024 bits)
    std::vector<uint8_t> reloc(128, 0);
    int reloc_count = 0;

    // Scan for instructions that need relocation
    // We mark the HIGH byte of 16-bit addresses
    for (size_t i = 0; i < code_size; i++) {
        uint8_t opcode = code[i];
        bool need_reloc = false;
        size_t addr_offset = 0;

        // JP nn (C3)
        if (opcode == 0xC3 && i + 2 < code_size) {
            uint16_t addr = code[i+1] | (code[i+2] << 8);
            // Only relocate if address is within our code
            if (addr < code_size) {
                need_reloc = true;
                addr_offset = i + 2;  // High byte
            }
            i += 2;
        }
        // CALL nn (CD)
        else if (opcode == 0xCD && i + 2 < code_size) {
            uint16_t addr = code[i+1] | (code[i+2] << 8);
            if (addr < code_size) {
                need_reloc = true;
                addr_offset = i + 2;
            }
            i += 2;
        }
        // JP cc,nn (C2, CA, D2, DA, E2, EA, F2, FA)
        else if ((opcode & 0xC7) == 0xC2 && i + 2 < code_size) {
            uint16_t addr = code[i+1] | (code[i+2] << 8);
            if (addr < code_size) {
                need_reloc = true;
                addr_offset = i + 2;
            }
            i += 2;
        }
        // CALL cc,nn (C4, CC, D4, DC, E4, EC, F4, FC)
        else if ((opcode & 0xC7) == 0xC4 && i + 2 < code_size) {
            uint16_t addr = code[i+1] | (code[i+2] << 8);
            if (addr < code_size) {
                need_reloc = true;
                addr_offset = i + 2;
            }
            i += 2;
        }
        // LD rr,nn (01, 11, 21, 31) - may load address
        else if ((opcode & 0xCF) == 0x01 && i + 2 < code_size) {
            uint16_t addr = code[i+1] | (code[i+2] << 8);
            // Only relocate if it looks like an internal address
            if (addr < code_size && addr > 0) {
                need_reloc = true;
                addr_offset = i + 2;
            }
            i += 2;
        }
        // LD (nn),A or LD A,(nn) - 32, 3A
        else if ((opcode == 0x32 || opcode == 0x3A) && i + 2 < code_size) {
            uint16_t addr = code[i+1] | (code[i+2] << 8);
            if (addr < code_size) {
                need_reloc = true;
                addr_offset = i + 2;
            }
            i += 2;
        }
        // LD (nn),HL or LD HL,(nn) - 22, 2A
        else if ((opcode == 0x22 || opcode == 0x2A) && i + 2 < code_size) {
            uint16_t addr = code[i+1] | (code[i+2] << 8);
            if (addr < code_size) {
                need_reloc = true;
                addr_offset = i + 2;
            }
            i += 2;
        }

        if (need_reloc && addr_offset < 1024) {
            size_t byte_idx = addr_offset / 8;
            size_t bit_idx = addr_offset % 8;
            reloc[byte_idx] |= (1 << bit_idx);
            reloc_count++;
        }
    }

    std::cout << "Found " << reloc_count << " relocatable addresses\n";

    // Create SPR file
    std::vector<uint8_t> spr;

    // Header (128 bytes)
    spr.resize(128, 0);
    spr[1] = code_size & 0xFF;           // psize low
    spr[2] = (code_size >> 8) & 0xFF;    // psize high
    spr[4] = bufsize & 0xFF;             // dsize low
    spr[5] = (bufsize >> 8) & 0xFF;      // dsize high

    // Relocation bitmap (128 bytes)
    spr.insert(spr.end(), reloc.begin(), reloc.end());

    // Code
    spr.insert(spr.end(), code.begin(), code.end());

    // Pad to 128-byte boundary
    while (spr.size() % 128 != 0) {
        spr.push_back(0);
    }

    // Write output
    std::ofstream out(output_file, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot create output: " << output_file << "\n";
        return 1;
    }

    out.write(reinterpret_cast<const char*>(spr.data()), spr.size());
    out.close();

    std::cout << "Output: " << output_file << " (" << spr.size() << " bytes)\n";
    std::cout << "  Program size: " << code_size << " bytes\n";
    std::cout << "  Buffer size: " << bufsize << " bytes\n";

    return 0;
}
