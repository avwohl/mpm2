// ssh_session_libssh.cpp - Non-blocking SSH using libssh
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifdef HAVE_LIBSSH

#include "ssh_session.h"
#include "console.h"
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <iostream>
#include <cstring>

// ============================================================================
// SSHSession - handles one connection with non-blocking I/O
// ============================================================================

SSHSession::SSHSession(int console_id, ssh_session session, ssh_channel channel)
    : console_id_(console_id)
    , session_(session)
    , channel_(channel)
    , active_(true)
    , sent_banner_(false)
{
    // Set channel to non-blocking
    ssh_channel_set_blocking(channel_, 0);
}

SSHSession::~SSHSession() {
    if (channel_) {
        ssh_channel_close(channel_);
        ssh_channel_free(channel_);
    }
    if (session_) {
        ssh_disconnect(session_);
        ssh_free(session_);
    }

    // Mark console as disconnected
    Console* con = ConsoleManager::instance().get(console_id_);
    if (con) {
        con->set_connected(false);
    }
}

bool SSHSession::poll() {
    if (!active_) return false;

    // Check if channel is still open
    if (ssh_channel_is_closed(channel_) || ssh_channel_is_eof(channel_)) {
        active_ = false;
        return false;
    }

    Console* con = ConsoleManager::instance().get(console_id_);
    if (!con) {
        active_ = false;
        return false;
    }

    // Send banner on first poll
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
        active_ = false;
        return false;
    }

    // Write from console output queue -> SSH
    int ch;
    while ((ch = con->output_queue().try_read()) >= 0) {
        char c = static_cast<char>(ch);
        if (ssh_channel_write(channel_, &c, 1) < 0) {
            active_ = false;
            return false;
        }
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

    // Set non-blocking mode
    ssh_bind_set_blocking(sshbind_, 0);

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
    if (rc == SSH_ERROR) {
        // No connection waiting or error
        ssh_free(session);
        return;
    }

    // Got a connection - handle key exchange (this may block briefly)
    if (ssh_handle_key_exchange(session) != SSH_OK) {
        std::cerr << "[SSH] Key exchange failed: " << ssh_get_error(session) << "\n";
        ssh_disconnect(session);
        ssh_free(session);
        return;
    }

    // Handle authentication (accept any for now)
    ssh_message message;
    bool authenticated = false;
    int auth_attempts = 0;

    while (!authenticated && auth_attempts < 20) {
        message = ssh_message_get(session);
        if (!message) break;

        if (ssh_message_type(message) == SSH_REQUEST_AUTH) {
            // Accept any authentication
            ssh_message_auth_reply_success(message, 0);
            authenticated = true;
        } else {
            ssh_message_reply_default(message);
        }
        ssh_message_free(message);
        auth_attempts++;
    }

    if (!authenticated) {
        ssh_disconnect(session);
        ssh_free(session);
        return;
    }

    // Wait for channel open request
    ssh_channel channel = nullptr;
    bool shell_requested = false;
    int channel_attempts = 0;

    while (!shell_requested && channel_attempts < 50) {
        message = ssh_message_get(session);
        if (!message) {
            channel_attempts++;
            continue;
        }

        switch (ssh_message_type(message)) {
        case SSH_REQUEST_CHANNEL_OPEN:
            if (ssh_message_subtype(message) == SSH_CHANNEL_SESSION) {
                channel = ssh_message_channel_request_open_reply_accept(message);
            } else {
                ssh_message_reply_default(message);
            }
            break;
        case SSH_REQUEST_CHANNEL:
            if (channel && (ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_SHELL ||
                           ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_PTY)) {
                ssh_message_channel_request_reply_success(message);
                if (ssh_message_subtype(message) == SSH_CHANNEL_REQUEST_SHELL) {
                    shell_requested = true;
                }
            } else {
                ssh_message_reply_default(message);
            }
            break;
        default:
            ssh_message_reply_default(message);
        }
        ssh_message_free(message);
        channel_attempts++;
    }

    if (!channel || !shell_requested) {
        if (channel) {
            ssh_channel_close(channel);
            ssh_channel_free(channel);
        }
        ssh_disconnect(session);
        ssh_free(session);
        return;
    }

    // Find a free console
    Console* con = ConsoleManager::instance().find_free();
    if (!con) {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        ssh_disconnect(session);
        ssh_free(session);
        return;
    }

    con->set_connected(true);

    // Create session
    auto ssh_session_obj = std::make_unique<SSHSession>(con->id(), session, channel);
    sessions_.push_back(std::move(ssh_session_obj));

    std::cerr << "[SSH] New connection on console " << con->id() << "\n";
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
