// mkboot.cpp - Create MP/M II boot image for emulator
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Creates a boot image with components at their correct addresses:
//   0x0100 - MPMLDR.COM (CP/M loader)
//   0x1700 - LDRBIOS (loader BIOS for boot phase)
//   0xBA00 - BNKXIOS (banked XIOS for runtime, optional)
//   0xFB00 - XIOS (extended I/O system jump table)
//
// Usage: mkboot -l ldrbios.bin -x xios.bin -b bnkxios.bin -m mpmldr.com -o boot.img

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <getopt.h>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  -l, --ldrbios FILE   LDRBIOS binary (loaded at 0x1700)\n"
              << "  -x, --xios FILE      XIOS binary (loaded at 0xFB00)\n"
              << "  -b, --bnkxios FILE   BNKXIOS binary (loaded at 0xBA00)\n"
              << "  -m, --mpmldr FILE    MPMLDR.COM (loaded at 0x0100)\n"
              << "  -o, --output FILE    Output boot image\n"
              << "  -h, --help           Show this help\n"
              << "\n";
}

bool load_file(const std::string& filename, std::vector<uint8_t>& data) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open " << filename << "\n";
        return false;
    }

    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    data.resize(size);
    file.read(reinterpret_cast<char*>(data.data()), size);

    return true;
}

int main(int argc, char* argv[]) {
    std::string ldrbios_file;
    std::string xios_file;
    std::string bnkxios_file;
    std::string mpmldr_file;
    std::string output_file;
    size_t output_size = 65536;  // Default to full 64KB

    static struct option long_options[] = {
        {"ldrbios", required_argument, nullptr, 'l'},
        {"xios",    required_argument, nullptr, 'x'},
        {"bnkxios", required_argument, nullptr, 'b'},
        {"mpmldr",  required_argument, nullptr, 'm'},
        {"output",  required_argument, nullptr, 'o'},
        {"size",    required_argument, nullptr, 's'},
        {"help",    no_argument,       nullptr, 'h'},
        {nullptr,   0,                 nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "l:x:b:m:o:s:h", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'l': ldrbios_file = optarg; break;
            case 'x': xios_file = optarg; break;
            case 'b': bnkxios_file = optarg; break;
            case 'm': mpmldr_file = optarg; break;
            case 'o': output_file = optarg; break;
            case 's': output_size = std::stoul(optarg); break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (output_file.empty()) {
        std::cerr << "Error: Output file required (-o)\n";
        print_usage(argv[0]);
        return 1;
    }

    // Create 64KB memory image (all 0x00)
    std::vector<uint8_t> image(65536, 0x00);

    // Set up page zero with boot entry
    // 0x0000: JP to MPMLDR at 0x0100 (boot entry point)
    // Note: After MP/M II is running, 0x0000 will be overwritten to point to WBOOT
    image[0x0000] = 0xC3;  // JP
    image[0x0001] = 0x00;
    image[0x0002] = 0x01;  // JP 0x0100 (MPMLDR entry)

    // 0x0003: I/O byte (not used much)
    image[0x0003] = 0x00;

    // 0x0004: Current disk/user
    image[0x0004] = 0x00;

    // 0x0005: JP to BDOS entry
    // MPMLDR has its own LDRBDOS at 0x032E (found by searching for the
    // BDOS dispatch pattern: LD HL,ret; PUSH HL; LD A,C; CP max; dispatch)
    image[0x0005] = 0xC3;  // JP
    image[0x0006] = 0x2E;
    image[0x0007] = 0x03;  // JP 0x032E (MPMLDR's internal LDRBDOS)

    // RST 1 (0x0008): Timer interrupt handler
    // This is used by z80_runner for 60Hz ticks. SYSINIT will overwrite this
    // with the proper handler once MP/M II initializes.
    // For now, just do EI + RET to allow boot to proceed.
    image[0x0008] = 0xFB;  // EI
    image[0x0009] = 0xC9;  // RET

    // RST 7 / RST 38H (0x0038): JP to XIOS tick handler at 0xFC80
    // (Also set up RST 7 for potential debugger use)
    image[0x0038] = 0xC3;  // JP
    image[0x0039] = 0x80;
    image[0x003A] = 0xFC;  // 0xFC80

    // LDRBIOS has its own jump table at 0x1700, MPMLDR calls it directly
    // No redirect table needed at 0xC300

    // 0x005C-0x007F: Default FCB (for MPMLDR to find MPM.SYS)
    // Set up FCB for "MPM     SYS" on drive A:
    image[0x005C] = 0x00;  // Drive A: (0 = default)
    // Filename: "MPM     " (8 chars, space padded)
    image[0x005D] = 'M';
    image[0x005E] = 'P';
    image[0x005F] = 'M';
    image[0x0060] = ' ';
    image[0x0061] = ' ';
    image[0x0062] = ' ';
    image[0x0063] = ' ';
    image[0x0064] = ' ';
    // Extension: "SYS"
    image[0x0065] = 'S';
    image[0x0066] = 'Y';
    image[0x0067] = 'S';

    // 0x0080: Default DMA buffer (command line tail)

    // Load MPMLDR at 0x0100
    if (!mpmldr_file.empty()) {
        std::vector<uint8_t> mpmldr;
        if (!load_file(mpmldr_file, mpmldr)) {
            return 1;
        }

        // MPMLDR.COM from distribution has 128 bytes of zeros at start
        // NOTE: Boot via MPMLDR (-b option) is not working. Use -s option instead.
        // The padding skip logic is kept but boot functionality needs debugging.
        size_t skip = 0;
        if (mpmldr.size() > 128) {
            bool all_zeros = true;
            for (size_t i = 0; i < 128 && all_zeros; i++) {
                if (mpmldr[i] != 0) all_zeros = false;
            }
            if (all_zeros && mpmldr[128] != 0) {
                skip = 128;
                std::cout << "Skipping 128-byte zero header in MPMLDR\n";
            }
        }
        size_t load_size = mpmldr.size() - skip;
        if (load_size > 0xF000 - 0x0100) {
            std::cerr << "Error: MPMLDR too large\n";
            return 1;
        }

        std::memcpy(&image[0x0100], mpmldr.data() + skip, load_size);
        std::cout << "Loaded MPMLDR at 0x0100 (" << load_size << " bytes)\n";
    }

    // Load LDRBIOS at 0x1700 (as expected by MPMLDR's LDRBDOS)
    if (!ldrbios_file.empty()) {
        std::vector<uint8_t> ldrbios;
        if (!load_file(ldrbios_file, ldrbios)) {
            return 1;
        }

        // ul80 linker outputs files with ORG padding (zeros from 0x0000 to ORG).
        // If file is larger than 0x1700, it contains the ORG padding - skip it.
        size_t skip = 0;
        size_t code_size = ldrbios.size();
        if (ldrbios.size() > 0x1700) {
            skip = 0x1700;
            code_size = ldrbios.size() - 0x1700;
            std::cout << "LDRBIOS has ORG padding, skipping first 0x1700 bytes\n";
        }

        // LDRBIOS must fit before high memory (max ~2KB of actual code)
        if (code_size > 0x1000) {  // 4KB max
            std::cerr << "Error: LDRBIOS too large (max 4096 bytes, got " << code_size << ")\n";
            return 1;
        }

        std::memcpy(&image[0x1700], ldrbios.data() + skip, code_size);
        std::cout << "Loaded LDRBIOS at 0x1700 (" << code_size << " bytes)\n";
    }

    // Load XIOS at 0xFC00
    if (!xios_file.empty()) {
        std::vector<uint8_t> xios;
        if (!load_file(xios_file, xios)) {
            return 1;
        }

        if (xios.size() > 0x10000 - 0xFC00) {
            std::cerr << "Error: XIOS too large\n";
            return 1;
        }

        std::memcpy(&image[0xFC00], xios.data(), xios.size());
        std::cout << "Loaded XIOS at 0xFC00 (" << xios.size() << " bytes)\n";
    }

    // Write output file
    std::ofstream out(output_file, std::ios::binary);
    if (!out) {
        std::cerr << "Error: Cannot create " << output_file << "\n";
        return 1;
    }

    out.write(reinterpret_cast<const char*>(image.data()), output_size);
    std::cout << "Created boot image: " << output_file << " (" << output_size << " bytes)\n";

    return 0;
}
