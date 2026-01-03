// ssh_session.h - SSH session handling for MP/M II terminals
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Non-blocking, single-threaded SSH implementation using polling.

#ifndef SSH_SESSION_H
#define SSH_SESSION_H

#ifdef HAVE_LIBSSH

#include <string>
#include <vector>
#include <memory>

class Console;

// Forward declarations for libssh types
typedef struct ssh_session_struct* ssh_session;
typedef struct ssh_bind_struct* ssh_bind;
typedef struct ssh_channel_struct* ssh_channel;

// SSH session - handles one SSH connection (non-blocking)
class SSHSession {
public:
    SSHSession(int console_id, ssh_session session, ssh_channel channel);
    ~SSHSession();

    // Poll for I/O - call from main loop
    // Returns false when session should be removed
    bool poll();

    bool is_active() const { return active_; }
    int console_id() const { return console_id_; }

private:
    int console_id_;
    ssh_session session_;
    ssh_channel channel_;
    bool active_;
    bool sent_banner_;
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

private:
    // Try to accept a new connection (non-blocking)
    void poll_accept();

    // Poll all active sessions
    void poll_sessions();

    ssh_bind sshbind_;
    int port_;
    bool running_;

    std::vector<std::unique_ptr<SSHSession>> sessions_;
};

#endif // HAVE_LIBSSH

#endif // SSH_SESSION_H
