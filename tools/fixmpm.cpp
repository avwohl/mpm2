// fixmpm.cpp - Fix incomplete MPM.SYS by padding to expected size
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// MPM.SYS header contains nmb_records at offset 120-121.
// If the file is shorter than expected, pad it to the correct size.

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <MPM.SYS>\n";
        return 1;
    }

    std::fstream file(argv[1], std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open " << argv[1] << "\n";
        return 1;
    }

    // Read header
    std::vector<uint8_t> header(256);
    file.read(reinterpret_cast<char*>(header.data()), 256);

    // Get nmb_records from offset 120-121 (little-endian)
    uint16_t nmb_records = header[120] | (header[121] << 8);
    size_t expected_size = nmb_records * 128;

    // Get actual file size
    file.seekg(0, std::ios::end);
    size_t actual_size = file.tellg();

    std::cout << "MPM.SYS analysis:\n";
    std::cout << "  nmb_records: " << nmb_records << " (0x" << std::hex << nmb_records << std::dec << ")\n";
    std::cout << "  Expected size: " << expected_size << " bytes\n";
    std::cout << "  Actual size: " << actual_size << " bytes\n";
    std::cout << "  Difference: " << (expected_size - actual_size) << " bytes\n";

    // Display key header fields
    std::cout << "\nHeader fields:\n";
    std::cout << "  mem_top: 0x" << std::hex << (int)header[0] << "00\n";
    std::cout << "  nmb_cns: " << std::dec << (int)header[1] << "\n";
    std::cout << "  brkpt_RST: " << (int)header[2] << "\n";
    std::cout << "  sys_call_stks: " << (header[3] ? "Y" : "N") << "\n";
    std::cout << "  bank_switched: " << (header[4] ? "Y" : "N") << "\n";
    std::cout << "  z80_cpu: " << (header[5] ? "Y" : "N") << "\n";
    std::cout << "  banked_bdos: " << (header[6] ? "Y" : "N") << "\n";
    std::cout << "  xios_jmp_tbl_base: 0x" << std::hex << (int)header[7] << "00\n";
    std::cout << "  resbdos_base: 0x" << (int)header[8] << "00\n";
    std::cout << "  xdos_base: 0x" << (int)header[11] << "00\n";
    std::cout << "  rsp_base: 0x" << (int)header[12] << "00\n";
    std::cout << "  bnkxios_base: 0x" << (int)header[13] << "00\n";
    std::cout << "  bnkbdos_base: 0x" << (int)header[14] << "00\n";
    std::cout << "  nmb_mem_seg: " << std::dec << (int)header[15] << "\n";
    std::cout << "  common_base: 0x" << std::hex << (int)header[124] << "00\n";
    std::cout << "  tmp_base: 0x" << (int)header[247] << "00\n";
    std::cout << "  bnkxdos_base: 0x" << (int)header[242] << "00\n";

    if (actual_size < expected_size) {
        std::cout << std::dec << "\nPadding file to " << expected_size << " bytes...\n";

        // Seek to end and pad with zeros
        file.seekp(0, std::ios::end);
        std::vector<uint8_t> padding(expected_size - actual_size, 0x00);
        file.write(reinterpret_cast<char*>(padding.data()), padding.size());

        std::cout << "Done. File is now " << expected_size << " bytes.\n";
    } else if (actual_size > expected_size) {
        std::cout << "\nWarning: File is larger than expected!\n";
    } else {
        std::cout << "\nFile size is correct.\n";
    }

    return 0;
}
