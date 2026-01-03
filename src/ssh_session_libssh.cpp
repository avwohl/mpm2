// ssh_session_libssh.cpp - Non-blocking SSH using libssh
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Uses OS-level non-blocking I/O (fcntl O_NONBLOCK) on all file descriptors.
// No libssh blocking mode calls - pure non-blocking via fcntl and ssh_event.

#ifdef HAVE_LIBSSH

#include "ssh_session.h"
#include "console.h"
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

// Set fd to non-blocking at OS level
static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// ============================================================================
// SSHSession - handles one connection with non-blocking I/O
// ============================================================================

SSHSession::SSHSession(ssh_session session)
    : session_(session)
    , channel_(nullptr)
    , event_(nullptr)
    , state_(SSHState::KEY_EXCHANGE)
    , console_id_(-1)
    , kex_done_(false)
    , sent_banner_(false)
{
    // Create event context (don't add session until after KEX)
    event_ = ssh_event_new();
}

SSHSession::~SSHSession() {
    // Release console
    if (console_id_ >= 0) {
        Console* con = ConsoleManager::instance().get(console_id_);
        if (con) {
            con->set_connected(false);
        }
    }

    if (event_) {
        if (session_ && kex_done_) {
            ssh_event_remove_session(event_, session_);
        }
        ssh_event_free(event_);
    }
    if (channel_) {
        ssh_channel_close(channel_);
        ssh_channel_free(channel_);
    }
    if (session_) {
        ssh_disconnect(session_);
        ssh_free(session_);
    }
}

bool SSHSession::poll() {
    if (state_ == SSHState::CLOSED) return false;

    // Poll events if kex done
    if (kex_done_ && event_) {
        int rc = ssh_event_dopoll(event_, 0);  // 0 = don't block
        if (rc == SSH_ERROR) {
            state_ = SSHState::CLOSED;
            return false;
        }
    }

    if (state_ == SSHState::READY) {
        return poll_io();
    } else {
        return poll_handshake();
    }
}

bool SSHSession::poll_handshake() {
    switch (state_) {
        case SSHState::KEY_EXCHANGE: {
            int rc = ssh_handle_key_exchange(session_);
            if (rc == SSH_OK) {
                kex_done_ = true;
                // Now safe to add to event
                if (event_) {
                    ssh_event_add_session(event_, session_);
                }
                state_ = SSHState::AUTHENTICATING;
            } else if (rc == SSH_ERROR) {
                std::cerr << "[SSH] Key exchange failed: " << ssh_get_error(session_) << "\n";
                state_ = SSHState::CLOSED;
                return false;
            }
            // SSH_AGAIN means still in progress
            break;
        }

        case SSHState::AUTHENTICATING:
        case SSHState::CHANNEL_OPEN: {
            ssh_message msg = ssh_message_get(session_);
            if (msg) {
                int type = ssh_message_type(msg);
                int subtype = ssh_message_subtype(msg);

                if (type == SSH_REQUEST_AUTH) {
                    // Accept any authentication
                    ssh_message_auth_reply_success(msg, 0);
                    state_ = SSHState::CHANNEL_OPEN;
                } else if (type == SSH_REQUEST_CHANNEL_OPEN) {
                    if (subtype == SSH_CHANNEL_SESSION) {
                        channel_ = ssh_message_channel_request_open_reply_accept(msg);
                    } else {
                        ssh_message_reply_default(msg);
                    }
                } else if (type == SSH_REQUEST_CHANNEL && channel_) {
                    if (subtype == SSH_CHANNEL_REQUEST_PTY) {
                        ssh_message_channel_request_reply_success(msg);
                    } else if (subtype == SSH_CHANNEL_REQUEST_SHELL) {
                        ssh_message_channel_request_reply_success(msg);

                        // Find a free console
                        Console* con = ConsoleManager::instance().find_free();
                        if (!con) {
                            std::cerr << "[SSH] No free console\n";
                            state_ = SSHState::CLOSED;
                            ssh_message_free(msg);
                            return false;
                        }

                        console_id_ = con->id();
                        con->set_connected(true);
                        state_ = SSHState::READY;

                        std::cerr << "[SSH] New connection on console " << console_id_ << "\n";
                    } else {
                        ssh_message_reply_default(msg);
                    }
                } else {
                    ssh_message_reply_default(msg);
                }
                ssh_message_free(msg);
            }
            break;
        }

        default:
            break;
    }

    return state_ != SSHState::CLOSED;
}

bool SSHSession::poll_io() {
    if (!channel_) {
        state_ = SSHState::CLOSED;
        return false;
    }

    // Check if channel is still open
    if (ssh_channel_is_closed(channel_) || ssh_channel_is_eof(channel_)) {
        state_ = SSHState::CLOSED;
        return false;
    }

    Console* con = ConsoleManager::instance().get(console_id_);
    if (!con) {
        state_ = SSHState::CLOSED;
        return false;
    }

    // Send banner on first I/O poll
    if (!sent_banner_) {
        char banner[64];
        snprintf(banner, sizeof(banner), "\r\nMP/M II Console %d\r\n\r\n", console_id_);
        ssh_channel_write(channel_, banner, strlen(banner));
        sent_banner_ = true;
    }

    // Read from SSH -> console input queue (non-blocking)
    char buf[256];
    int n = ssh_channel_read_nonblocking(channel_, buf, sizeof(buf), 0);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            con->input_queue().try_write(static_cast<uint8_t>(buf[i]));
        }
    } else if (n == SSH_ERROR) {
        state_ = SSHState::CLOSED;
        return false;
    }

    // Write from console output queue -> SSH
    int ch;
    int write_count = 0;
    while ((ch = con->output_queue().try_read()) >= 0) {
        char c = static_cast<char>(ch);
        if (ssh_channel_write(channel_, &c, 1) < 0) {
            state_ = SSHState::CLOSED;
            return false;
        }
        write_count++;
    }
    static int poll_count = 0;
    if (++poll_count % 1000 == 0 || write_count > 0) {
        std::cerr << "[SSH] poll#" << poll_count << " con=" << console_id_
                  << " out_avail=" << con->output_queue().available()
                  << " wrote=" << write_count << "\n";
    }

    return true;
}

// ============================================================================
// SSHServer - accepts connections with non-blocking polling
// ============================================================================

SSHServer::SSHServer()
    : sshbind_(nullptr)
    , port_(0)
    , running_(false)
{
}

SSHServer::~SSHServer() {
    stop();
}

bool SSHServer::init(const std::string& host_key_path) {
    sshbind_ = ssh_bind_new();
    if (!sshbind_) {
        std::cerr << "Failed to create SSH bind\n";
        return false;
    }

    // Set host key
    if (ssh_bind_options_set(sshbind_, SSH_BIND_OPTIONS_HOSTKEY, host_key_path.c_str()) < 0) {
        std::cerr << "Failed to set host key: " << ssh_get_error(sshbind_) << "\n";
        ssh_bind_free(sshbind_);
        sshbind_ = nullptr;
        return false;
    }

    return true;
}

bool SSHServer::listen(int port) {
    if (!sshbind_) return false;

    port_ = port;

    if (ssh_bind_options_set(sshbind_, SSH_BIND_OPTIONS_BINDPORT, &port_) < 0) {
        std::cerr << "Failed to set port: " << ssh_get_error(sshbind_) << "\n";
        return false;
    }

    if (ssh_bind_listen(sshbind_) < 0) {
        std::cerr << "Failed to listen: " << ssh_get_error(sshbind_) << "\n";
        return false;
    }

    // Set bind socket non-blocking at OS level
    socket_t bind_fd = ssh_bind_get_fd(sshbind_);
    if (bind_fd != SSH_INVALID_SOCKET) {
        set_nonblocking(bind_fd);
    }

    running_ = true;
    return true;
}

void SSHServer::stop() {
    running_ = false;
    sessions_.clear();

    if (sshbind_) {
        ssh_bind_free(sshbind_);
        sshbind_ = nullptr;
    }
}

void SSHServer::poll() {
    if (!running_) return;

    poll_accept();
    poll_sessions();
}

void SSHServer::poll_accept() {
    // Try to accept a new connection (non-blocking)
    ssh_session session = ssh_new();
    if (!session) return;

    int rc = ssh_bind_accept(sshbind_, session);
    if (rc != SSH_OK) {
        // No connection waiting or error
        ssh_free(session);
        return;
    }

    // Got a connection - set fd non-blocking at OS level
    socket_t fd = ssh_get_fd(session);
    if (fd != SSH_INVALID_SOCKET) {
        set_nonblocking(fd);
    }

    // Create session object - handshake will proceed in poll()
    sessions_.push_back(std::make_unique<SSHSession>(session));
}

void SSHServer::poll_sessions() {
    // Poll all sessions and remove inactive ones
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [](std::unique_ptr<SSHSession>& s) {
                           return !s->poll();
                       }),
        sessions_.end());
}

size_t SSHServer::session_count() const {
    return sessions_.size();
}

#endif // HAVE_LIBSSH
