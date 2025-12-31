// ssh_session.h - SSH session handling for MP/M II terminals
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SSH_SESSION_H
#define SSH_SESSION_H

#if defined(HAVE_LIBSSH) || defined(HAVE_WOLFSSH)

#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <memory>
#include <functional>

class Console;

#ifdef HAVE_LIBSSH
// Forward declarations for libssh types
typedef struct ssh_session_struct* ssh_session;
typedef struct ssh_bind_struct* ssh_bind;
typedef struct ssh_channel_struct* ssh_channel;
typedef struct ssh_event_struct* ssh_event;
#else
// Forward declarations for wolfSSH types
struct WOLFSSH_CTX;
struct WOLFSSH;
struct WOLFSSH_CHANNEL;
struct WS_UserAuthData;
using byte = unsigned char;
#endif

// SSH session - handles one SSH connection
class SSHSession {
public:
#ifdef HAVE_LIBSSH
    SSHSession(int console_id, ssh_session session, ssh_channel channel);
#else
    SSHSession(int console_id, WOLFSSH* ssh, int fd);
#endif
    ~SSHSession();

    void start();
    void stop();
    void join();
    bool is_running() const { return running_.load(); }
    int console_id() const { return console_id_; }

private:
    void thread_func();

    int console_id_;
#ifdef HAVE_LIBSSH
    ssh_session session_;
    ssh_channel channel_;
#else
    WOLFSSH* ssh_;
    int fd_;
#endif
    std::thread thread_;
    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;
};

// SSH server - accepts connections and spawns sessions
class SSHServer {
public:
    SSHServer();
    ~SSHServer();

    bool init(const std::string& host_key_path,
              const std::string& authorized_keys_path = "");

    using AuthCallback = std::function<bool(const std::string&, const std::string&)>;
    void set_auth_callback(AuthCallback cb) { auth_callback_ = cb; }

    bool listen(int port);
    void stop();
    void accept_loop();
    bool is_running() const { return running_.load(); }
    size_t session_count() const;

private:
    void cleanup_sessions();

#ifdef HAVE_LIBSSH
    ssh_bind sshbind_;
#else
    static int user_auth_callback(byte auth_type, WS_UserAuthData* auth_data, void* ctx);
    static int channel_shell_callback(WOLFSSH_CHANNEL* channel, void* ctx);
    WOLFSSH_CTX* ctx_;
    int listen_fd_;
#endif
    int port_;

    std::vector<std::unique_ptr<SSHSession>> sessions_;
    AuthCallback auth_callback_;

    std::atomic<bool> running_;
    std::atomic<bool> stop_requested_;
};

#endif // HAVE_LIBSSH || HAVE_WOLFSSH

#endif // SSH_SESSION_H
