// main.cpp - MP/M II Emulator entry point
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "console.h"
#include "z80_thread.h"
#include "disk.h"

#ifdef HAVE_WOLFSSH
#include "ssh_session.h"
#endif

#include <iostream>
#include <string>
#include <vector>
#include <csignal>
#include <cstdlib>
#include <getopt.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

// Global flag for clean shutdown
static volatile sig_atomic_t g_shutdown_requested = 0;

void signal_handler(int sig) {
    (void)sig;
    g_shutdown_requested = 1;
}


// Terminal raw mode handling for local console
static struct termios g_orig_termios;
static bool g_termios_saved = false;

static void restore_terminal() {
    if (g_termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_termios_saved = false;
    }
}

static bool setup_raw_terminal() {
    if (!isatty(STDIN_FILENO)) {
        return false;  // Not a terminal
    }

    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        return false;
    }
    g_termios_saved = true;
    atexit(restore_terminal);

    struct termios raw = g_orig_termios;
    // Disable canonical mode, echo, and signal chars
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    // Disable Ctrl-S/Q flow control
    raw.c_iflag &= ~(IXON | ICRNL);
    // Non-blocking read: return immediately if no input
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
        return false;
    }
    return true;
}

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  -p, --port PORT       SSH listen port (default: 2222)\n"
              << "  -k, --key FILE        Host key file in DER format (default: keys/ssh_host_rsa_key.der)\n"
              << "  -d, --disk A:FILE     Mount disk image on drive A-P\n"
              << "  -b, --boot FILE       Boot image file (MPMLDR + MPM.SYS)\n"
              << "  -x, --xios ADDR       XIOS base address in hex (default: FC00)\n"
              << "  -l, --local           Enable local console (output to stdout)\n"
              << "  -h, --help            Show this help\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " -d A:system.dsk -d B:work.dsk\n"
              << "  " << prog << " -p 2222 -k mykey.pem -d A:mpm2.dsk\n"
              << "  " << prog << " -l -b boot.img -d A:system.dsk\n"
              << "\n";
}

int main(int argc, char* argv[]) {
    // Default options
    int ssh_port = 2222;
    std::string host_key = "keys/ssh_host_rsa_key.der";
    std::string boot_image;
    uint16_t xios_base = 0x8800;
    bool local_console = false;
    std::vector<std::pair<int, std::string>> disk_mounts;

    // Parse command line options
    static struct option long_options[] = {
        {"port",  required_argument, nullptr, 'p'},
        {"key",   required_argument, nullptr, 'k'},
        {"disk",  required_argument, nullptr, 'd'},
        {"boot",  required_argument, nullptr, 'b'},
        {"xios",  required_argument, nullptr, 'x'},
        {"local", no_argument,       nullptr, 'l'},
        {"help",  no_argument,       nullptr, 'h'},
        {nullptr, 0,                 nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:k:d:b:x:lh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'p':
                ssh_port = std::atoi(optarg);
                break;
            case 'k':
                host_key = optarg;
                break;
            case 'd': {
                // Format: A:filename or 0:filename
                std::string arg = optarg;
                if (arg.length() >= 2 && arg[1] == ':') {
                    int drive;
                    if (arg[0] >= 'A' && arg[0] <= 'P') {
                        drive = arg[0] - 'A';
                    } else if (arg[0] >= 'a' && arg[0] <= 'p') {
                        drive = arg[0] - 'a';
                    } else if (arg[0] >= '0' && arg[0] <= '9') {
                        drive = arg[0] - '0';
                    } else {
                        std::cerr << "Invalid drive letter: " << arg[0] << "\n";
                        return 1;
                    }
                    disk_mounts.push_back({drive, arg.substr(2)});
                } else {
                    std::cerr << "Invalid disk specification: " << arg << "\n";
                    return 1;
                }
                break;
            }
            case 'b':
                boot_image = optarg;
                break;
            case 'x':
                xios_base = std::strtoul(optarg, nullptr, 16);
                break;
            case 'l':
                local_console = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Set up signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Force unbuffered output for non-TTY environments
    std::ios::sync_with_stdio(true);
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    std::cout << "MP/M II Emulator\n";
    std::cout << "================\n\n";

    // Initialize console manager
    ConsoleManager::instance().init();
    std::cout << "Initialized " << MAX_CONSOLES << " consoles\n";

    // Enable local console mode if requested
    // Enable on all consoles since MP/M II may use any console for boot output
    if (local_console) {
        for (int i = 0; i < MAX_CONSOLES; i++) {
            Console* con = ConsoleManager::instance().get(i);
            if (con) {
                con->set_local_mode(true);
            }
        }
        std::cout << "Local console enabled on all " << MAX_CONSOLES << " consoles\n";
    }

    // Mount disks
    for (const auto& mount : disk_mounts) {
        if (DiskSystem::instance().mount(mount.first, mount.second)) {
            Disk* disk = DiskSystem::instance().get(mount.first);
            const char* fmt_name = "unknown";
            if (disk) {
                switch (disk->format()) {
                    case DiskFormat::SSSD_8: fmt_name = "8\" SSSD"; break;
                    case DiskFormat::HD1K:   fmt_name = "hd1k (8MB)"; break;
                    case DiskFormat::HD512:  fmt_name = "hd512"; break;
                    case DiskFormat::CUSTOM: fmt_name = "custom"; break;
                }
            }
            std::cout << "Mounted " << mount.second << " as drive "
                      << static_cast<char>('A' + mount.first) << ": [" << fmt_name << "]\n";
        } else {
            std::cerr << "Failed to mount " << mount.second << "\n";
        }
    }

    // Initialize Z80 thread
    Z80Thread z80;
    if (!z80.init(boot_image)) {
        std::cerr << "Failed to initialize Z80 emulator\n";
        if (!boot_image.empty()) {
            std::cerr << "Could not load boot image: " << boot_image << "\n";
        }
        return 1;
    }

    z80.set_xios_base(xios_base);
    std::cout << "XIOS base: 0x" << std::hex << xios_base << std::dec << "\n";

#ifdef HAVE_WOLFSSH
    // Initialize SSH server (skip if only using local console)
    SSHServer ssh_server;
    bool ssh_enabled = false;
    if (!local_console) {
        if (!ssh_server.init(host_key)) {
            std::cerr << "Failed to initialize SSH server\n";
            std::cerr << "Make sure host key exists: " << host_key << "\n";
            std::cerr << "Generate with: ssh-keygen -t rsa -f " << host_key << " -N ''\n";
            return 1;
        }

        if (!ssh_server.listen(ssh_port)) {
            std::cerr << "Failed to listen on port " << ssh_port << "\n";
            return 1;
        }

        ssh_enabled = true;
        std::cout << "SSH server listening on port " << ssh_port << "\n";
        std::cout << "Connect with: ssh -p " << ssh_port << " user@localhost\n\n";
    } else {
        std::cout << "Running in local console mode (SSH disabled)\n\n";
    }
#else
    std::cout << "SSH support not available (wolfSSH not found)\n";
    std::cout << "Running in local mode only\n\n";
    (void)ssh_port;
    (void)host_key;
#endif

    // Start Z80 thread
    if (!boot_image.empty()) {
        std::cout << "Starting Z80 CPU...\n";
        z80.start();
    } else {
        std::cout << "No boot image specified - CPU not started\n";
        std::cout << "Use -b option to specify boot image\n";
    }

    // Main loop
    std::cout << "\nPress Ctrl+C to shutdown\n\n";

#ifdef HAVE_WOLFSSH
    if (ssh_enabled) {
        // Run SSH accept loop in main thread (blocks until shutdown)
        ssh_server.accept_loop();
    } else {
        // Local console mode - read from stdin and broadcast to all local consoles
        if (setup_raw_terminal()) {
            while (!g_shutdown_requested) {
                // Poll stdin for input
                char ch;
                ssize_t n = read(STDIN_FILENO, &ch, 1);
                if (n > 0) {
                    // Handle Ctrl+C for shutdown
                    if (ch == 0x03) {
                        g_shutdown_requested = 1;
                        break;
                    }
                    // Broadcast to all local mode consoles
                    for (int i = 0; i < MAX_CONSOLES; i++) {
                        Console* con = ConsoleManager::instance().get(i);
                        if (con && con->is_local()) {
                            con->input_queue().try_write(static_cast<uint8_t>(ch));
                        }
                    }
                } else {
                    // No input available - sleep briefly to avoid busy-wait
                    usleep(1000);  // 1ms
                }
            }
            restore_terminal();
        } else {
            // Not a TTY - just wait for shutdown signal
            while (!g_shutdown_requested) {
                struct timeval tv;
                tv.tv_sec = 1;
                tv.tv_usec = 0;
                select(0, nullptr, nullptr, nullptr, &tv);
            }
        }
    }
#else
    // Local console mode - read from stdin and broadcast to all local consoles
    if (setup_raw_terminal()) {
        while (!g_shutdown_requested) {
            // Poll stdin for input
            char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n > 0) {
                // Handle Ctrl+C for shutdown
                if (ch == 0x03) {
                    g_shutdown_requested = 1;
                    break;
                }
                // Broadcast to all local mode consoles
                for (int i = 0; i < MAX_CONSOLES; i++) {
                    Console* con = ConsoleManager::instance().get(i);
                    if (con && con->is_local()) {
                        con->input_queue().try_write(static_cast<uint8_t>(ch));
                    }
                }
            } else {
                // No input available - sleep briefly to avoid busy-wait
                usleep(1000);  // 1ms
            }
        }
        restore_terminal();
    } else {
        // Not a TTY - just wait for shutdown signal
        while (!g_shutdown_requested) {
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            select(0, nullptr, nullptr, nullptr, &tv);
        }
    }
#endif

    std::cout << "\nShutting down...\n";

    // Stop Z80
    z80.stop();

#ifdef HAVE_WOLFSSH
    // Stop SSH server
    ssh_server.stop();
#endif

    std::cout << "Z80 executed " << z80.instructions() << " instructions\n";
    std::cout << "Goodbye!\n";

    return 0;
}
