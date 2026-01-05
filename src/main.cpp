// main.cpp - MP/M II Emulator entry point
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "console.h"
#include "z80_runner.h"
#include "disk.h"

#if defined(HAVE_LIBSSH) || defined(HAVE_WOLFSSH)
#include "ssh_session.h"
#include "sftp_bridge.h"
#define HAVE_SSH
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
#include <chrono>

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
    std::cerr << "Usage: " << prog << " [options] -d A:diskimage\n"
              << "\n"
              << "Options:\n"
              << "  -d, --disk A:FILE     Mount disk image on drive A-P (required)\n"
              << "  -l, --local           Enable local console (output to stdout)\n"
              << "  -t, --timeout SECS    Timeout in seconds for debugging (0 = no timeout)\n"
#ifdef HAVE_SSH
              << "  -p, --port PORT       SSH listen port (default: 2222)\n"
              << "  -k, --key FILE        Host key file (default: keys/ssh_host_rsa_key)\n"
              << "  -a, --authorized-keys FILE  Authorized keys file (default: keys/authorized_keys)\n"
              << "  -n, --no-auth         Disable SSH authentication (accept any connection)\n"
              << "  --test-rsp            Test SFTP RSP communication (runs after 5 sec boot delay)\n"
#endif
              << "  -h, --help            Show this help\n"
              << "\n"
              << "The emulator boots from disk sector 0 of drive A.\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " -l -d A:system.img           # Local console mode\n"
#ifdef HAVE_SSH
              << "  " << prog << " -d A:system.img              # SSH mode (connect with ssh -p 2222)\n"
#endif
              << "\n";
}

int main(int argc, char* argv[]) {
    // Default options
    bool local_console = false;
    int timeout_seconds = 0;
    std::vector<std::pair<int, std::string>> disk_mounts;
#ifdef HAVE_SSH
    int ssh_port = 2222;
    std::string host_key = "keys/ssh_host_rsa_key";
    std::string authorized_keys = "keys/authorized_keys";
    bool no_auth = false;
    bool test_rsp = false;
#endif

    // Parse command line options
    static struct option long_options[] = {
        {"disk",          required_argument, nullptr, 'd'},
        {"local",         no_argument,       nullptr, 'l'},
        {"timeout",       required_argument, nullptr, 't'},
#ifdef HAVE_SSH
        {"port",          required_argument, nullptr, 'p'},
        {"key",           required_argument, nullptr, 'k'},
        {"authorized-keys", required_argument, nullptr, 'a'},
        {"no-auth",       no_argument,       nullptr, 'n'},
        {"test-rsp",      no_argument,       nullptr, 'T'},
#endif
        {"help",          no_argument,       nullptr, 'h'},
        {nullptr,         0,                 nullptr, 0}
    };

    int opt;
#ifdef HAVE_SSH
    const char* optstring = "d:lt:p:k:a:nTh";
#else
    const char* optstring = "d:lt:h";
#endif
    while ((opt = getopt_long(argc, argv, optstring, long_options, nullptr)) != -1) {
        switch (opt) {
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
            case 'l':
                local_console = true;
                break;
            case 't':
                timeout_seconds = std::atoi(optarg);
                break;
#ifdef HAVE_SSH
            case 'p':
                ssh_port = std::atoi(optarg);
                break;
            case 'k':
                host_key = optarg;
                break;
            case 'a':
                authorized_keys = optarg;
                break;
            case 'n':
                no_auth = true;
                break;
            case 'T':
                test_rsp = true;
                break;
#endif
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Require at least one disk mount
    if (disk_mounts.empty()) {
        std::cerr << "Error: No disk mounted. Use -d A:diskimage to mount a boot disk.\n\n";
        print_usage(argv[0]);
        return 1;
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
            return 1;
        }
    }

    // Initialize Z80 and boot from disk
    Z80Runner z80;

    if (!z80.boot_from_disk()) {
        std::cerr << "Failed to boot from disk\n";
        return 1;
    }

    if (timeout_seconds > 0) {
        std::cout << "Setting boot timeout: " << timeout_seconds << " seconds\n";
        z80.set_timeout(timeout_seconds);
    }

#ifdef HAVE_SSH
    // Initialize SSH server (skip if using local console)
    SSHServer ssh_server;
    bool ssh_enabled = false;
    if (!local_console) {
        if (!ssh_server.init(host_key)) {
            std::cerr << "Failed to initialize SSH server\n";
            std::cerr << "Make sure host key exists: " << host_key << "\n";
            std::cerr << "Generate with: ssh-keygen -t rsa -f " << host_key << " -N ''\n";
            return 1;
        }

        // Configure authentication
        if (no_auth) {
            ssh_server.set_no_auth(true);
            std::cout << "SSH authentication disabled (--no-auth)\n";
        } else {
            if (!ssh_server.load_authorized_keys(authorized_keys)) {
                std::cerr << "Warning: No authorized keys loaded from " << authorized_keys << "\n";
                std::cerr << "Copy your public key: cp ~/.ssh/id_rsa.pub " << authorized_keys << "\n";
                std::cerr << "Or use --no-auth to disable authentication\n";
                return 1;
            }
        }

        if (!ssh_server.listen(ssh_port)) {
            std::cerr << "Failed to listen on port " << ssh_port << "\n";
            return 1;
        }

        ssh_enabled = true;
        std::cout << "SSH server listening on port " << ssh_port << "\n";
        std::cout << "Connect with: ssh -p " << ssh_port << " user@localhost\n\n";

        // Set up Z80 tick callback for SFTP bridge
        // This allows wait_for_reply() to run Z80 cycles while waiting
        SftpBridge::instance().set_z80_tick_callback([&z80]() {
            z80.run_polled();
        });
    }
#endif

    // Main loop
    std::cout << "\nPress Ctrl+C to shutdown\n\n";

    if (local_console) {
        // Local console mode - read from stdin and run CPU
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
                    // Send input to console 3 (first user console)
                    Console* con = ConsoleManager::instance().get(3);
                    if (con && con->is_local()) {
                        con->input_queue().try_write(static_cast<uint8_t>(ch));
                    }
                }
                // Run CPU
                if (!z80.run_polled()) break;
            }
            restore_terminal();
        } else {
            // Not a TTY - use select() for input with timeout
            fd_set rfds;
            while (!g_shutdown_requested && !z80.timed_out()) {
                FD_ZERO(&rfds);
                FD_SET(STDIN_FILENO, &rfds);
                struct timeval tv = {0, 0};  // No wait
                int ret = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
                if (ret > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
                    char ch;
                    ssize_t n = read(STDIN_FILENO, &ch, 1);
                    if (n > 0) {
                        // Convert LF to CR for CP/M compatibility
                        if (ch == '\n') ch = '\r';
                        Console* con = ConsoleManager::instance().get(3);
                        if (con && con->is_local()) {
                            con->input_queue().try_write(static_cast<uint8_t>(ch));
                        }
                    }
                }
                // Run CPU
                if (!z80.run_polled()) break;
            }
        }
    }
#ifdef HAVE_SSH
    else if (ssh_enabled) {
        // SSH mode - poll SSH and CPU in single thread
        auto start_time = std::chrono::steady_clock::now();
        int rsp_test_state = 0;  // 0=wait, 1=sent, 2=done
        int rsp_test_delay_sec = 15;
        uint32_t rsp_test_id = 0;
        auto rsp_test_send_time = start_time;

        while (!g_shutdown_requested && !z80.timed_out()) {
            ssh_server.poll();  // Check for new connections and session I/O
            if (!z80.run_polled()) break;

            // Run RSP test after delay if requested (non-blocking)
            if (test_rsp && rsp_test_state < 2) {
                auto elapsed = std::chrono::steady_clock::now() - start_time;

                if (rsp_test_state == 0 &&
                    std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= rsp_test_delay_sec) {
                    // Send test request
                    rsp_test_state = 1;
                    rsp_test_send_time = std::chrono::steady_clock::now();
                    std::cout << "\n=== SFTP RSP Communication Test ===\n";
                    std::cout << "Sending TEST request to RSP...\n";

                    SftpRequest req;
                    req.type = SftpRequestType::TEST;
                    req.drive = 0;
                    req.user = 0;
                    req.flags = 0;
                    req.offset = 0;
                    req.length = 0;
                    rsp_test_id = SftpBridge::instance().enqueue_request(std::move(req));
                }

                if (rsp_test_state == 1) {
                    // Check for reply (non-blocking)
                    auto reply = SftpBridge::instance().try_get_reply(rsp_test_id);
                    if (reply) {
                        rsp_test_state = 2;
                        if (reply->status == SftpReplyStatus::OK) {
                            std::cout << "SUCCESS: RSP responded!\n";
                            std::cout << "=== RSP Test PASSED ===\n\n";
                        } else {
                            std::cout << "FAILED: RSP returned error status\n";
                            std::cout << "=== RSP Test FAILED ===\n\n";
                        }
                    } else {
                        // Check for timeout
                        auto wait_time = std::chrono::steady_clock::now() - rsp_test_send_time;
                        if (std::chrono::duration_cast<std::chrono::seconds>(wait_time).count() >= 10) {
                            rsp_test_state = 2;
                            std::cout << "FAILED: Timeout waiting for RSP reply\n";
                            std::cout << "(RSP may not be running or XIOS dispatch not working)\n";
                            std::cout << "=== RSP Test FAILED ===\n\n";
                        }
                    }

                    // Exit after test completes if no SSH connections
                    if (rsp_test_state == 2 && ssh_server.session_count() == 0) {
                        g_shutdown_requested = 1;
                    }
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

    z80.request_stop();

#ifdef HAVE_SSH
    ssh_server.stop();
#endif

    std::cout << "Z80 executed " << z80.instructions() << " instructions\n";
    std::cout << "Goodbye!\n";

    return 0;
}
