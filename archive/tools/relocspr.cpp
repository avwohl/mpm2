// relocspr.cpp - Relocate SPR file to target address
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MP/M SPR file format:
//   Byte 0: 0 (indicates relocatable)
//   Byte 1: Original base page (0 = page-relative, else absolute base)
//   Bytes 2-3: Code size in 256-byte pages (little-endian)
//   Bytes 4-255: Padding (zeros)
//   Bytes 256 to 256+size-1: Code
//   Bytes 256+size onwards: Relocation bitmap (1 bit per code byte)
//
// For each bit set in the bitmap, the corresponding code byte has
// the target base page added to it.

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " input.spr output.bin base_page\n"
              << "\n"
              << "Relocates an SPR file to the specified base page.\n"
              << "\n"
              << "Arguments:\n"
              << "  input.spr   Input SPR file\n"
              << "  output.bin  Output relocated binary\n"
              << "  base_page   Target base page (hex, e.g., CE for 0xCE00)\n"
              << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        print_usage(argv[0]);
        return 1;
    }

    const char* input_file = argv[1];
    const char* output_file = argv[2];
    unsigned int base_page = 0;

    // Parse base page as hex
    if (sscanf(argv[3], "%x", &base_page) != 1) {
        std::cerr << "Invalid base page: " << argv[3] << "\n";
        return 1;
    }

    // Read input SPR file
    std::ifstream in(input_file, std::ios::binary);
    if (!in) {
        std::cerr << "Cannot open input: " << input_file << "\n";
        return 1;
    }

    in.seekg(0, std::ios::end);
    size_t file_size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> spr(file_size);
    in.read(reinterpret_cast<char*>(spr.data()), file_size);
    in.close();

    // Parse header
    uint8_t marker = spr[0];
    uint8_t orig_page = spr[1];
    uint16_t size_pages = spr[2] | (spr[3] << 8);
    size_t code_size = size_pages * 256;

    std::cout << "Input: " << input_file << " (" << file_size << " bytes)\n";
    std::cout << "  Marker: " << (int)marker << "\n";
    std::cout << "  Original base page: 0x" << std::hex << (int)orig_page << std::dec << "\n";
    std::cout << "  Code size: " << size_pages << " pages (" << code_size << " bytes)\n";
    std::cout << "  Target base page: 0x" << std::hex << base_page << std::dec << "\n";

    if (file_size < 256 + code_size) {
        std::cerr << "File too small for declared code size\n";
        return 1;
    }

    // Extract code (starts at offset 256)
    std::vector<uint8_t> code(spr.begin() + 256, spr.begin() + 256 + code_size);

    // Relocation bitmap starts after code
    size_t bitmap_offset = 256 + code_size;
    size_t bitmap_size = (file_size > bitmap_offset) ? (file_size - bitmap_offset) : 0;

    std::cout << "  Bitmap offset: " << bitmap_offset << ", size: " << bitmap_size << " bytes\n";

    // Apply relocation
    // For each bit set in the bitmap, add (base_page - orig_page) to the code byte
    int reloc_count = 0;
    int8_t delta = (int8_t)(base_page - orig_page);

    std::cout << "  Relocation delta: " << (int)delta << " (0x" << std::hex << (int)(uint8_t)delta << std::dec << ")\n";

    for (size_t byte_idx = 0; byte_idx < bitmap_size && byte_idx * 8 < code_size; byte_idx++) {
        uint8_t bitmap_byte = spr[bitmap_offset + byte_idx];
        for (int bit = 0; bit < 8; bit++) {
            size_t code_idx = byte_idx * 8 + bit;
            if (code_idx >= code_size) break;

            if (bitmap_byte & (1 << bit)) {
                code[code_idx] += delta;
                reloc_count++;
            }
        }
    }

    std::cout << "  Relocated " << reloc_count << " bytes\n";

    // Write output
    std::ofstream out(output_file, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot create output: " << output_file << "\n";
        return 1;
    }

    out.write(reinterpret_cast<const char*>(code.data()), code.size());
    out.close();

    std::cout << "Output: " << output_file << " (" << code.size() << " bytes)\n";

    return 0;
}
