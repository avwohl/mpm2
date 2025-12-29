// ssh_session.cpp - SSH session implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HAVE_WOLFSSH

#include "ssh_session.h"
#include "console.h"

#include <wolfssh/ssh.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <iostream>

// SSHSession implementation

SSHSession::SSHSession(int console_id, WOLFSSH* ssh, int fd)
    : console_id_(console_id)
    , ssh_(ssh)
    , fd_(fd)
    , running_(false)
    , stop_requested_(false)
{
}

SSHSession::~SSHSession() {
    stop();
    join();
    if (ssh_) {
        wolfSSH_shutdown(ssh_);
        wolfSSH_free(ssh_);
    }
    if (fd_ >= 0) {
        close(fd_);
    }
}

void SSHSession::start() {
    if (running_.load()) return;
    running_.store(true);
    stop_requested_.store(false);
    thread_ = std::thread(&SSHSession::thread_func, this);
}

void SSHSession::stop() {
    stop_requested_.store(true);
}

void SSHSession::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SSHSession::thread_func() {
    Console* con = ConsoleManager::instance().get(console_id_);
    if (!con) {
        running_.store(false);
        return;
    }

    // Wait for channel to be ready by processing worker until we can send
    // wolfSSH needs to process channel open requests before stream I/O works
    int ret;
    int attempts = 0;
    while (attempts++ < 100) {
        ret = wolfSSH_worker(ssh_, nullptr);
        int err = wolfSSH_get_error(ssh_);
        if (ret == WS_SUCCESS || ret == WS_CHAN_RXD || ret > 0) {
            break;
        }
        if (err == WS_EOF) {
            running_.store(false);
            return;
        }
        if (err != WS_WANT_READ && err != WS_WANT_WRITE &&
            err != WS_CHAN_RXD && err != WS_SUCCESS && err != 0) {
            running_.store(false);
            return;
        }
        // Small delay to avoid busy loop
        struct timeval tv = {0, 10000};
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd_, &rfds);
        select(fd_ + 1, &rfds, nullptr, nullptr, &tv);
    }

    con->set_connected(true);

    // Send banner
    char banner[128];
    snprintf(banner, sizeof(banner),
             "\r\nMP/M II Console %d\r\n\r\n", console_id_);
    int send_ret = wolfSSH_stream_send(ssh_, reinterpret_cast<byte*>(banner), strlen(banner));
    if (send_ret < 0) {
        std::cerr << "[SSH] Banner send failed: " << wolfSSH_get_error(ssh_) << "\n";
    }

    uint8_t buf[256];

    while (!stop_requested_.load()) {
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);

        FD_SET(fd_, &rfds);

        // Check if we have output to send
        if (con->output_queue().available() > 0) {
            FD_SET(fd_, &wfds);
        }

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 10000;  // 10ms timeout

        int sel_ret = select(fd_ + 1, &rfds, &wfds, nullptr, &tv);
        if (sel_ret < 0) break;

        // Read from SSH -> input queue
        if (FD_ISSET(fd_, &rfds)) {
            int n = wolfSSH_stream_read(ssh_, buf, sizeof(buf));
            if (n <= 0) {
                int err = wolfSSH_get_error(ssh_);
                if (err == WS_EOF || n == WS_EOF) {
                    break;  // Client disconnected
                }
                if (err != WS_WANT_READ && err != WS_WANT_WRITE) {
                    break;  // Error
                }
                continue;
            }

            // Queue characters for MP/M
            for (int i = 0; i < n; i++) {
                uint8_t ch = buf[i];
                // Convert LF to CR for CP/M compatibility
                if (ch == '\n') ch = '\r';
                con->input_queue().try_write(ch);
            }
        }

        // Write from output queue -> SSH
        if (FD_ISSET(fd_, &wfds)) {
            size_t count = con->output_queue().read_some(buf, sizeof(buf));
            if (count > 0) {
                wolfSSH_stream_send(ssh_, buf, count);
            }
        }
    }

    con->reset();
    running_.store(false);
}

// SSHServer implementation

SSHServer::SSHServer()
    : ctx_(nullptr)
    , listen_fd_(-1)
    , port_(0)
    , running_(false)
    , stop_requested_(false)
{
}

SSHServer::~SSHServer() {
    stop();

    // Clean up sessions
    for (auto& session : sessions_) {
        session->stop();
        session->join();
    }
    sessions_.clear();

    if (ctx_) {
        wolfSSH_CTX_free(ctx_);
    }
    if (listen_fd_ >= 0) {
        close(listen_fd_);
    }

    wolfSSH_Cleanup();
}

bool SSHServer::init(const std::string& host_key_path,
                     const std::string& /* authorized_keys_path */) {
    // Initialize wolfSSH
    if (wolfSSH_Init() != WS_SUCCESS) {
        std::cerr << "wolfSSH_Init failed\n";
        return false;
    }

    // Create context
    ctx_ = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, nullptr);
    if (!ctx_) {
        std::cerr << "wolfSSH_CTX_new failed\n";
        return false;
    }

    // Load host key
    std::ifstream key_file(host_key_path, std::ios::binary);
    if (!key_file) {
        std::cerr << "Cannot open key file: " << host_key_path << "\n";
        return false;
    }

    key_file.seekg(0, std::ios::end);
    size_t key_size = key_file.tellg();
    key_file.seekg(0, std::ios::beg);

    std::vector<uint8_t> key_data(key_size);
    key_file.read(reinterpret_cast<char*>(key_data.data()), key_size);

    int ret = wolfSSH_CTX_UsePrivateKey_buffer(ctx_, key_data.data(), key_size,
                                                WOLFSSH_FORMAT_ASN1);
    if (ret != WS_SUCCESS) {
        // Try PEM format
        ret = wolfSSH_CTX_UsePrivateKey_buffer(ctx_, key_data.data(), key_size,
                                                WOLFSSH_FORMAT_PEM);
        if (ret != WS_SUCCESS) {
            std::cerr << "wolfSSH_CTX_UsePrivateKey_buffer failed: " << ret << "\n";
            return false;
        }
    }

    // Set up authentication callback
    wolfSSH_SetUserAuth(ctx_, user_auth_callback);

    // Set up channel shell request callback
    wolfSSH_CTX_SetChannelReqShellCb(ctx_, channel_shell_callback);

    return true;
}

bool SSHServer::listen(int port) {
    port_ = port;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        return false;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr),
             sizeof(addr)) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (::listen(listen_fd_, 5) < 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    return true;
}

void SSHServer::stop() {
    stop_requested_.store(true);
    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
    }
}

void SSHServer::accept_loop() {
    running_.store(true);

    while (!stop_requested_.load()) {
        // Clean up finished sessions
        cleanup_sessions();

        // Set up for select with timeout
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd_, &rfds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int ret = select(listen_fd_ + 1, &rfds, nullptr, nullptr, &tv);
        if (ret <= 0) continue;

        // Accept connection
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_,
                               reinterpret_cast<struct sockaddr*>(&client_addr),
                               &client_len);
        if (client_fd < 0) continue;

        // Find a free console
        Console* con = ConsoleManager::instance().find_free();
        if (!con) {
            // No free console - reject connection
            close(client_fd);
            continue;
        }

        // Create SSH session
        WOLFSSH* ssh = wolfSSH_new(ctx_);
        if (!ssh) {
            close(client_fd);
            continue;
        }

        wolfSSH_set_fd(ssh, client_fd);

        // Perform SSH handshake
        int accept_ret;
        do {
            accept_ret = wolfSSH_accept(ssh);
        } while (accept_ret == WS_WANT_READ || accept_ret == WS_WANT_WRITE);

        if (accept_ret != WS_SUCCESS) {
            wolfSSH_free(ssh);
            close(client_fd);
            continue;
        }

        // Create and start session
        auto session = std::make_unique<SSHSession>(con->id(), ssh, client_fd);
        session->start();
        sessions_.push_back(std::move(session));
    }

    running_.store(false);
}

size_t SSHServer::session_count() const {
    size_t count = 0;
    for (const auto& session : sessions_) {
        if (session->is_running()) count++;
    }
    return count;
}

void SSHServer::cleanup_sessions() {
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [](const std::unique_ptr<SSHSession>& s) {
                           return !s->is_running();
                       }),
        sessions_.end());
}

int SSHServer::user_auth_callback(byte auth_type, WS_UserAuthData* auth_data, void* ctx) {
    (void)ctx;
    (void)auth_data;

    // Accept any authentication - no password verification
    // This is an emulator for testing, not a production system
    if (auth_type == WOLFSSH_USERAUTH_PASSWORD ||
        auth_type == WOLFSSH_USERAUTH_PUBLICKEY ||
        auth_type == WOLFSSH_USERAUTH_NONE) {
        return WOLFSSH_USERAUTH_SUCCESS;
    }

    return WOLFSSH_USERAUTH_INVALID_AUTHTYPE;
}

int SSHServer::channel_shell_callback(WOLFSSH_CHANNEL* channel, void* ctx) {
    (void)channel;
    (void)ctx;
    // Accept shell request
    return WS_SUCCESS;
}

#endif // HAVE_WOLFSSH
