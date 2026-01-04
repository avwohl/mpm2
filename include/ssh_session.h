// ssh_session.h - SSH session handling for MP/M II terminals
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Non-blocking, single-threaded SSH implementation using polling.
// Uses OS-level non-blocking I/O (fcntl O_NONBLOCK) and ssh_event API.

#ifndef SSH_SESSION_H
#define SSH_SESSION_H

#ifdef HAVE_LIBSSH

#include <string>
#include <vector>
#include <memory>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>

class Console;

// Session state during handshake
enum class SSHState {
    KEY_EXCHANGE,
    AUTHENTICATING,
    CHANNEL_OPEN,
    READY,
    CLOSED
};

class SSHServer;  // Forward declaration

// SSH session - handles one SSH connection (non-blocking)
class SSHSession {
public:
    SSHSession(ssh_session session, SSHServer* server);
    ~SSHSession();

    // Poll for handshake progress and I/O - call from main loop
    // Returns false when session should be removed
    bool poll();

    bool is_active() const { return state_ != SSHState::CLOSED; }
    bool is_ready() const { return state_ == SSHState::READY; }
    int console_id() const { return console_id_; }

    // Accessors for authentication callbacks
    SSHServer* server() const;
    void set_authenticated(bool auth);
    void set_channel(ssh_channel channel);
    void setup_channel_callbacks(ssh_channel channel);
    void setup_console();

private:
    bool poll_handshake();
    bool poll_io();

    ssh_session session_;
    ssh_channel channel_;
    ssh_event event_;
    SSHState state_;
    int console_id_;
    bool kex_done_;
    bool sent_banner_;
    bool authenticated_;
    SSHServer* server_;
    ssh_server_callbacks_struct server_callbacks_;
    ssh_channel_callbacks_struct channel_callbacks_;
};

// SSH server - accepts connections (non-blocking)
class SSHServer {
public:
    SSHServer();
    ~SSHServer();

    bool init(const std::string& host_key_path);
    bool listen(int port);
    void stop();

    // Poll for new connections and session I/O - call from main loop
    void poll();

    bool is_running() const { return running_; }
    size_t session_count() const;

    // Authentication settings
    void set_no_auth(bool no_auth) { no_auth_ = no_auth; }
    bool load_authorized_keys(const std::string& path);
    bool is_key_authorized(const std::string& key_blob) const;
    bool no_auth() const { return no_auth_; }

private:
    // Try to accept a new connection (non-blocking)
    void poll_accept();

    // Poll all active sessions
    void poll_sessions();

    ssh_bind sshbind_;
    int port_;
    bool running_;
    bool no_auth_ = false;
    std::vector<std::string> authorized_keys_;  // Base64 encoded public keys

    std::vector<std::unique_ptr<SSHSession>> sessions_;
};

#endif // HAVE_LIBSSH

#endif // SSH_SESSION_H
