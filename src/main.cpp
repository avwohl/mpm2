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

// Global debug flag (non-static, accessible from other translation units)
bool g_debug_enabled = false;

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
              << "  -s, --sys FILE        Load MPM.SYS directly (bypass MPMLDR)\n"
              << "  -l, --local           Enable local console (output to stdout)\n"
              << "  -t, --timeout SECS    Timeout in seconds for debugging (0 = no timeout)\n"
              << "  -D, --debug           Enable debug output (XIOS, disk operations)\n"
              << "  -h, --help            Show this help\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " -d A:system.dsk -d B:work.dsk\n"
              << "  " << prog << " -p 2222 -k mykey.pem -d A:mpm2.dsk\n"
              << "  " << prog << " -l -b boot.img -d A:system.dsk\n"
              << "  " << prog << " -l -s MPM.SYS -d A:system.dsk  # Direct load\n"
              << "  " << prog << " -t 5 -l -b boot.img -d A:system.dsk  # 5 second timeout\n"
              << "\n";
}

int main(int argc, char* argv[]) {
    // Default options
    int ssh_port = 2222;
    std::string host_key = "keys/ssh_host_rsa_key.der";
    std::string boot_image;
    std::string mpm_sys_file;  // Direct MPM.SYS loading (bypasses MPMLDR)
    bool local_console = false;
    int timeout_seconds = 0;
    std::vector<std::pair<int, std::string>> disk_mounts;

    // Parse command line options
    static struct option long_options[] = {
        {"port",    required_argument, nullptr, 'p'},
        {"key",     required_argument, nullptr, 'k'},
        {"disk",    required_argument, nullptr, 'd'},
        {"boot",    required_argument, nullptr, 'b'},
        {"sys",     required_argument, nullptr, 's'},
        {"local",   no_argument,       nullptr, 'l'},
        {"timeout", required_argument, nullptr, 't'},
        {"debug",   no_argument,       nullptr, 'D'},
        {"help",    no_argument,       nullptr, 'h'},
        {nullptr,   0,                 nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "p:k:d:b:s:lt:Dh", long_options, nullptr)) != -1) {
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
            case 's':
                mpm_sys_file = optarg;
                break;
            case 'l':
                local_console = true;
                break;
            case 't':
                timeout_seconds = std::atoi(optarg);
                break;
            case 'D':
                g_debug_enabled = true;
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
    std::signal(SIGPIPE, SIG_IGN);  // Ignore SIGPIPE from closed SSH connections

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
    // Enable output on ALL consoles (MP/M II uses multiple consoles for TMPs)
    // Input will be directed to console 0 only (see input handling below)
    if (local_console) {
        for (int i = 0; i < MAX_CONSOLES; i++) {
            Console* con = ConsoleManager::instance().get(i);
            if (con) {
                con->set_local_mode(true);
            }
        }
        std::cout << "Local console enabled on all " << MAX_CONSOLES << " consoles (input to console 3)\n";
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

    // Check for conflicting options
    if (!boot_image.empty() && !mpm_sys_file.empty()) {
        std::cerr << "Cannot specify both -b (boot image) and -s (MPM.SYS)\n";
        return 1;
    }

    if (!mpm_sys_file.empty()) {
        // Direct MPM.SYS loading - bypass MPMLDR
        if (!z80.init("")) {  // Initialize without boot image
            std::cerr << "Failed to initialize Z80 emulator\n";
            return 1;
        }
        if (!z80.load_mpm_sys(mpm_sys_file)) {
            std::cerr << "Failed to load MPM.SYS: " << mpm_sys_file << "\n";
            return 1;
        }
        // XIOS base is set by load_mpm_sys based on SYSTEM.DAT
    } else {
        // Traditional boot via MPMLDR
        if (!z80.init(boot_image)) {
            std::cerr << "Failed to initialize Z80 emulator\n";
            if (!boot_image.empty()) {
                std::cerr << "Could not load boot image: " << boot_image << "\n";
            }
            return 1;
        }
    }

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
        std::cout << "Local console mode (-l flag)\n\n";
    }
#else
    std::cout << "SSH not compiled in - local console only\n\n";
    (void)ssh_port;
    (void)host_key;

    // Auto-enable local mode on all consoles when SSH not available
    // Output from all consoles, input to console 0 only
    for (int i = 0; i < MAX_CONSOLES; i++) {
        Console* con = ConsoleManager::instance().get(i);
        if (con) {
            con->set_local_mode(true);
        }
    }
#endif

    // Start Z80 thread
    if (!boot_image.empty() || !mpm_sys_file.empty()) {
        if (timeout_seconds > 0) {
            std::cout << "Setting boot timeout: " << timeout_seconds << " seconds\n";
            z80.set_timeout(timeout_seconds);
        }
        std::cout << "Starting Z80 CPU...\n";
        z80.start();
    } else {
        std::cout << "No boot image specified - CPU not started\n";
        std::cout << "Use -b option for boot image or -s for direct MPM.SYS loading\n";
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
            while (!g_shutdown_requested && !z80.timed_out()) {
                // Poll stdin for input
                char ch;
                ssize_t n = read(STDIN_FILENO, &ch, 1);
                if (n > 0) {
                    // Handle Ctrl+C for shutdown
                    if (ch == 0x03) {
                        g_shutdown_requested = 1;
                        break;
                    }
                    // Send input to ALL consoles - let MP/M decide which TMP gets it
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
            // Not a TTY - still read from stdin (for piped input) but with select() for timeout
            fd_set rfds;
            bool stdin_eof = false;
            while (!g_shutdown_requested && !z80.timed_out()) {
                if (stdin_eof) {
                    // Stdin closed - just sleep until shutdown/timeout
                    usleep(10000);  // 10ms
                    continue;
                }
                FD_ZERO(&rfds);
                FD_SET(STDIN_FILENO, &rfds);
                struct timeval tv;
                tv.tv_sec = 0;
                tv.tv_usec = 10000;  // 10ms
                int ret = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
                if (ret > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
                    char ch;
                    ssize_t n = read(STDIN_FILENO, &ch, 1);
                    if (n > 0) {
                        // Broadcast to all local consoles - MP/M II may use any console
                        for (int i = 0; i < MAX_CONSOLES; i++) {
                            Console* con = ConsoleManager::instance().get(i);
                            if (con && con->is_local()) {
                                con->input_queue().try_write(static_cast<uint8_t>(ch));
                            }
                        }
                    } else if (n == 0) {
                        // EOF on stdin - stop reading but don't exit
                        stdin_eof = true;
                    }
                }
            }
        }
    }
#else
    // Local console mode - read from stdin and broadcast to all consoles
    if (setup_raw_terminal()) {
        while (!g_shutdown_requested && !z80.timed_out()) {
            // Poll stdin for input
            char ch;
            ssize_t n = read(STDIN_FILENO, &ch, 1);
            if (n > 0) {
                // Handle Ctrl+C for shutdown
                if (ch == 0x03) {
                    g_shutdown_requested = 1;
                    break;
                }
                // Broadcast to all local consoles - MP/M II may use any console
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
        // Not a TTY - still read from stdin (for piped input) but with select() for timeout
        fd_set rfds;
        bool stdin_eof = false;
        while (!g_shutdown_requested && !z80.timed_out()) {
            if (stdin_eof) {
                // Stdin closed - just sleep until shutdown/timeout
                usleep(10000);  // 10ms
                continue;
            }
            FD_ZERO(&rfds);
            FD_SET(STDIN_FILENO, &rfds);
            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 10000;  // 10ms
            int ret = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
                char ch;
                ssize_t n = read(STDIN_FILENO, &ch, 1);
                if (n > 0) {
                    // Convert LF to CR for CP/M compatibility
                    if (ch == '\n') ch = '\r';
                    // Send input to ALL consoles - let MP/M decide which TMP gets it
                    for (int i = 0; i < MAX_CONSOLES; i++) {
                        Console* con = ConsoleManager::instance().get(i);
                        if (con && con->is_local()) {
                            con->input_queue().try_write(static_cast<uint8_t>(ch));
                        }
                    }
                } else if (n == 0) {
                    // EOF on stdin - stop reading but don't exit
                    stdin_eof = true;
                }
            }
        }
    }
#endif

    if (z80.timed_out()) {
        std::cout << "\nBoot timeout - shutting down...\n";
    } else {
        std::cout << "\nShutting down...\n";
    }

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
