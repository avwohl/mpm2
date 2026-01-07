// ssh_session.h - SSH session handling for MP/M II terminals
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Threaded SSH implementation - one thread per connection.
// Each session runs in its own thread using blocking libssh calls.
// Communicates with Z80 via thread-safe queues (ConsoleQueue, SftpBridge).

#ifndef SSH_SESSION_H
#define SSH_SESSION_H

#ifdef HAVE_LIBSSH

#include "listen_address.h"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <thread>
#include <atomic>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>
#include <libssh/sftp.h>

class Console;

// Simple directory entry for SFTP (used locally, populated via RSP bridge)
struct SftpDirEntry {
    std::string name;
    uint8_t user;
    uint32_t size;
    bool is_directory;
    bool is_system;
    bool is_read_only;
};

// Session state during handshake
enum class SSHState {
    KEY_EXCHANGE,
    AUTHENTICATING,
    CHANNEL_OPEN,
    SFTP_PENDING,  // SFTP subsystem requested, init pending
    READY,
    DRAINING,      // EOF received, waiting for channel to close
    CLOSED
};

class SSHServer;  // Forward declaration

// SSH session - handles one SSH connection in its own thread
class SSHSession {
public:
    SSHSession(ssh_session session, SSHServer* server);
    ~SSHSession();

    // Start the session thread
    void start();

    // Signal the session to stop (non-blocking)
    void request_stop() { stop_requested_ = true; }

    // Check if session is still running
    bool is_running() const { return running_; }

    // Wait for thread to finish (call after request_stop or when is_running is false)
    void join();

    bool is_active() const { return state_ != SSHState::CLOSED; }
    bool is_ready() const { return state_ == SSHState::READY; }
    int console_id() const { return console_id_; }

    // Accessors for authentication callbacks
    SSHServer* server() const;
    void set_authenticated(bool auth);
    void set_channel(ssh_channel channel);
    void setup_channel_callbacks(ssh_channel channel);
    void setup_console();

    // SFTP support
    void mark_sftp_pending() { state_ = SSHState::SFTP_PENDING; }
    void setup_sftp();
    bool is_sftp() const { return is_sftp_; }

    // Send banner to channel (called from shell_request callback)
    void send_banner(ssh_channel channel);

private:
    // Thread entry point
    void run();

    // Internal polling methods (called from run() in session thread)
    bool poll_handshake();
    bool poll_io();
    bool poll_sftp();

    // Thread management
    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};

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
    std::string client_ip_;

    // SFTP support
    sftp_session sftp_;
    bool is_sftp_;

    // SFTP directory handles
    struct OpenDir {
        int drive;
        int user;
        std::vector<SftpDirEntry> entries;
        size_t index;  // Current read position
        bool enumeration_complete = false;  // Directory listing complete?
    };
    std::map<void*, std::unique_ptr<OpenDir>> open_dirs_;

    // Pending SFTP operation state (for async RSP requests)
    struct PendingSftpOp {
        sftp_client_message msg = nullptr;  // Message awaiting reply
        uint32_t request_id = 0;            // Bridge request ID
        int op_type = 0;                    // SSH_FXP_* type
        void* handle = nullptr;             // Associated handle (for dir enum)
        bool search_first = true;           // For directory enumeration
    };
    PendingSftpOp pending_sftp_;

    // SFTP file handles
    struct OpenFile {
        int drive;
        int user;
        std::string filename;
        uint32_t size;
        uint64_t offset;        // Current read position
        bool is_read_only;
        bool is_write;          // File opened for writing
        bool file_created;      // File was created (new or truncated)
        // Cached file data - entire file read at open via RSP bridge
        // For write mode, this accumulates written data until close
        std::vector<uint8_t> cached_data;
    };
    std::map<void*, std::unique_ptr<OpenFile>> open_files_;

    uint32_t next_handle_id_ = 1;
};

// SSH server - accepts connections (non-blocking)
class SSHServer {
public:
    SSHServer();
    ~SSHServer();

    bool init(const std::string& host_key_path);
    bool listen(const std::string& host, int port);
    bool listen(int port) { return listen("", port); }
    bool listen(const ListenAddress& addr) { return listen(addr.host, addr.port); }
    void stop();

    // Poll for new connections and session I/O - call from main loop
    void poll();

    bool is_running() const { return running_; }
    size_t session_count() const;

    // Get listen address
    const ListenAddress& listen_address() const { return listen_addr_; }

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
    ListenAddress listen_addr_;
    bool running_;
    bool no_auth_ = false;
    std::vector<std::string> authorized_keys_;  // Base64 encoded public keys

    std::vector<std::unique_ptr<SSHSession>> sessions_;
};

#endif // HAVE_LIBSSH

#endif // SSH_SESSION_H
