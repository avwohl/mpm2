// mkspr.cpp - Create SPR file from binary
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Creates an SPR (System Page Relocatable) file from a raw binary.
// SPR format:
//   Bytes 0-127: Header
//     0: unused
//     1-2: psize (program size in bytes, little-endian)
//     3: unused
//     4-5: dsize (data/buffer size, little-endian)
//     6-127: unused (zeros)
//   Bytes 128-255: Relocation bitmap (1 bit per byte of code)
//   Bytes 256+: Code (page-aligned)
//
// For our emulator XIOS, we have no relocations since the emulator
// intercepts all calls. We set relocation bits to 0.

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " input.bin output.spr [bufsize]\n"
              << "\n"
              << "Creates an SPR file from a raw binary.\n"
              << "\n"
              << "Arguments:\n"
              << "  input.bin   Input binary file\n"
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

    // Create SPR file
    std::vector<uint8_t> spr;

    // Header (128 bytes)
    spr.resize(128, 0);
    spr[1] = code_size & 0xFF;           // psize low
    spr[2] = (code_size >> 8) & 0xFF;    // psize high
    spr[4] = bufsize & 0xFF;             // dsize low
    spr[5] = (bufsize >> 8) & 0xFF;      // dsize high

    // Relocation bitmap (128 bytes = 1024 bits, enough for 1K of code)
    // All zeros = no relocations
    for (int i = 0; i < 128; i++) {
        spr.push_back(0);
    }

    // Pad to page boundary if needed
    while (spr.size() < 256) {
        spr.push_back(0);
    }

    // Append code
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
