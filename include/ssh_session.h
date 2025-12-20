// ssh_session.h - SSH session handling for MP/M II terminals
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SSH_SESSION_H
#define SSH_SESSION_H

#ifdef HAVE_WOLFSSH

#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <functional>

// Forward declarations for wolfSSH types
struct WOLFSSH_CTX;
struct WOLFSSH;
struct WOLFSSH_CHANNEL;
struct WS_UserAuthData;

// wolfSSH uses 'byte' type
using byte = unsigned char;

class Console;

// SSH session - handles one SSH connection
class SSHSession {
public:
    SSHSession(int console_id, WOLFSSH* ssh, int fd);
    ~SSHSession();

    // Start the session thread
    void start();

    // Stop the session (non-blocking request)
    void stop();

    // Wait for session to finish
    void join();

    // Check if running
    bool is_running() const { return running_.load(); }

    // Get console ID
    int console_id() const { return console_id_; }

private:
    void thread_func();

    int console_id_;
    WOLFSSH* ssh_;
    int fd_;
    std::thread thread_;
    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;
};

// SSH server - accepts connections and spawns sessions
class SSHServer {
public:
    SSHServer();
    ~SSHServer();

    // Initialize the server
    // Returns false on failure
    bool init(const std::string& host_key_path,
              const std::string& authorized_keys_path = "");

    // Set authentication callback (username, password) -> bool
    using AuthCallback = std::function<bool(const std::string&, const std::string&)>;
    void set_auth_callback(AuthCallback cb) { auth_callback_ = cb; }

    // Start listening on port
    bool listen(int port);

    // Stop the server
    void stop();

    // Accept loop - call from main thread or spawn a thread
    void accept_loop();

    // Check if running
    bool is_running() const { return running_.load(); }

    // Get number of active sessions
    size_t session_count() const;

private:
    // Cleanup finished sessions
    void cleanup_sessions();

    // wolfSSH callbacks
    static int user_auth_callback(byte auth_type, WS_UserAuthData* auth_data, void* ctx);
    static int channel_shell_callback(WOLFSSH_CHANNEL* channel, void* ctx);

    WOLFSSH_CTX* ctx_;
    int listen_fd_;
    int port_;

    std::vector<std::unique_ptr<SSHSession>> sessions_;
    AuthCallback auth_callback_;

    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;
};

#endif // HAVE_WOLFSSH

#endif // SSH_SESSION_H
