// ssh_session_libssh.cpp - SSH session implementation using libssh
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HAVE_LIBSSH

#include "ssh_session.h"
#include "console.h"

#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>

#include <algorithm>
#include <cstring>
#include <iostream>

// SSHSession implementation

SSHSession::SSHSession(int console_id, ssh_session session, ssh_channel channel)
    : console_id_(console_id)
    , session_(session)
    , channel_(channel)
    , running_(false)
    , stop_requested_(false)
{
}

SSHSession::~SSHSession() {
    stop();
    join();
    if (channel_) {
        ssh_channel_send_eof(channel_);
        ssh_channel_close(channel_);
        ssh_channel_free(channel_);
    }
    if (session_) {
        ssh_disconnect(session_);
        ssh_free(session_);
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
        std::cerr << "[SSH:" << console_id_ << "] No console, exiting\n";
        running_.store(false);
        return;
    }

    std::cerr << "[SSH:" << console_id_ << "] Session starting\n";

    // Set session and channel to non-blocking
    ssh_set_blocking(session_, 0);
    ssh_channel_set_blocking(channel_, 0);

    // Send banner (blocking for initial output)
    ssh_set_blocking(session_, 1);
    char banner[128];
    snprintf(banner, sizeof(banner),
             "\r\nMP/M II Console %d\r\n\r\n", console_id_);
    ssh_channel_write(channel_, banner, strlen(banner));
    ssh_set_blocking(session_, 0);

    std::cerr << "[SSH:" << console_id_ << "] Entering main loop\n";

    uint8_t buf[256];

    while (!stop_requested_.load()) {
        // Check if channel is still open
        if (ssh_channel_is_closed(channel_) || ssh_channel_is_eof(channel_)) {
            break;
        }

        bool did_work = false;

        // Send any pending output from MP/M
        size_t count = con->output_queue().read_some(buf, sizeof(buf));
        if (count > 0) {
            size_t sent = 0;
            while (sent < count) {
                int written = ssh_channel_write(channel_, buf + sent, count - sent);
                if (written == SSH_ERROR) {
                    std::cerr << "[SSH:" << console_id_ << "] Write error: "
                              << ssh_get_error(session_) << "\n";
                    stop_requested_.store(true);
                    break;
                } else if (written == SSH_AGAIN) {
                    // Would block - try again after a short delay
                    struct timespec ts = {0, 1000000};  // 1ms
                    nanosleep(&ts, nullptr);
                } else if (written > 0) {
                    sent += written;
                    did_work = true;
                }
            }
        }

        // Read input from SSH client (non-blocking)
        int n = ssh_channel_read_nonblocking(channel_, buf, sizeof(buf), 0);
        if (n > 0) {
            // Queue characters for MP/M
            for (int i = 0; i < n; i++) {
                uint8_t ch = buf[i];
                if (ch == '\n') ch = '\r';  // LF -> CR for CP/M
                con->input_queue().try_write(ch);
            }
            did_work = true;
        } else if (n == SSH_ERROR) {
            std::cerr << "[SSH:" << console_id_ << "] Read error: "
                      << ssh_get_error(session_) << "\n";
            break;
        } else if (n == SSH_EOF) {
            break;
        }

        // Sleep only if we didn't do any work (avoid busy-waiting but stay responsive)
        if (!did_work) {
            struct timespec ts = {0, 10000000};  // 10ms
            nanosleep(&ts, nullptr);
        }
    }

    std::cerr << "[SSH:" << console_id_ << "] Session ending\n";
    con->reset();
    running_.store(false);
}

// SSHServer implementation

SSHServer::SSHServer()
    : sshbind_(nullptr)
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

    if (sshbind_) {
        ssh_bind_free(sshbind_);
    }
}

bool SSHServer::init(const std::string& host_key_path,
                     const std::string& /* authorized_keys_path */) {
    sshbind_ = ssh_bind_new();
    if (!sshbind_) {
        std::cerr << "ssh_bind_new failed\n";
        return false;
    }

    // Set host key
    if (ssh_bind_options_set(sshbind_, SSH_BIND_OPTIONS_HOSTKEY,
                             host_key_path.c_str()) < 0) {
        std::cerr << "Failed to set host key: " << ssh_get_error(sshbind_) << "\n";
        return false;
    }

    return true;
}

bool SSHServer::listen(int port) {
    port_ = port;

    // Set port
    if (ssh_bind_options_set(sshbind_, SSH_BIND_OPTIONS_BINDPORT, &port) < 0) {
        std::cerr << "Failed to set port: " << ssh_get_error(sshbind_) << "\n";
        return false;
    }

    // Listen
    if (ssh_bind_listen(sshbind_) < 0) {
        std::cerr << "Failed to listen: " << ssh_get_error(sshbind_) << "\n";
        return false;
    }

    return true;
}

void SSHServer::stop() {
    stop_requested_.store(true);
}

void SSHServer::accept_loop() {
    running_.store(true);

    while (!stop_requested_.load()) {
        cleanup_sessions();

        // Create a new session for the incoming connection
        ssh_session session = ssh_new();
        if (!session) {
            continue;
        }

        // Accept with timeout
        int rc = ssh_bind_accept(sshbind_, session);
        if (rc == SSH_ERROR) {
            ssh_free(session);
            continue;
        }

        // Key exchange
        if (ssh_handle_key_exchange(session) != SSH_OK) {
            std::cerr << "Key exchange failed: " << ssh_get_error(session) << "\n";
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        // Authentication - accept any for this emulator
        ssh_message message;
        bool authenticated = false;

        while (!authenticated && !stop_requested_.load()) {
            message = ssh_message_get(session);
            if (!message) break;

            switch (ssh_message_type(message)) {
            case SSH_REQUEST_AUTH:
                switch (ssh_message_subtype(message)) {
                case SSH_AUTH_METHOD_PASSWORD:
                case SSH_AUTH_METHOD_PUBLICKEY:
                case SSH_AUTH_METHOD_NONE:
                    // Accept all authentication
                    ssh_message_auth_reply_success(message, 0);
                    authenticated = true;
                    break;
                default:
                    ssh_message_auth_set_methods(message,
                        SSH_AUTH_METHOD_PASSWORD | SSH_AUTH_METHOD_PUBLICKEY);
                    ssh_message_reply_default(message);
                }
                break;
            default:
                ssh_message_reply_default(message);
            }
            ssh_message_free(message);
        }

        if (!authenticated) {
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        // Wait for channel request
        ssh_channel channel = nullptr;
        bool shell_requested = false;

        while (!shell_requested && !stop_requested_.load()) {
            message = ssh_message_get(session);
            if (!message) break;

            switch (ssh_message_type(message)) {
            case SSH_REQUEST_CHANNEL_OPEN:
                if (ssh_message_subtype(message) == SSH_CHANNEL_SESSION) {
                    channel = ssh_message_channel_request_open_reply_accept(message);
                }
                break;
            case SSH_REQUEST_CHANNEL:
                switch (ssh_message_subtype(message)) {
                case SSH_CHANNEL_REQUEST_PTY:
                    ssh_message_channel_request_reply_success(message);
                    break;
                case SSH_CHANNEL_REQUEST_SHELL:
                    ssh_message_channel_request_reply_success(message);
                    shell_requested = true;
                    break;
                default:
                    ssh_message_reply_default(message);
                }
                break;
            default:
                ssh_message_reply_default(message);
            }
            ssh_message_free(message);
        }

        if (!channel || !shell_requested) {
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        // Find a free console
        Console* con = ConsoleManager::instance().find_free();
        if (!con) {
            ssh_channel_close(channel);
            ssh_channel_free(channel);
            ssh_disconnect(session);
            ssh_free(session);
            continue;
        }

        con->set_connected(true);

        // Create and start session
        auto ssh_session_obj = std::make_unique<SSHSession>(con->id(), session, channel);
        ssh_session_obj->start();
        sessions_.push_back(std::move(ssh_session_obj));

        std::cerr << "[SSH] New connection on console " << con->id() << "\n";
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

#endif // HAVE_LIBSSH
