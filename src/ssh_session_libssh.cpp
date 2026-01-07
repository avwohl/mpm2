// ssh_session_libssh.cpp - Non-blocking SSH using libssh
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Uses OS-level non-blocking I/O (fcntl O_NONBLOCK) on all file descriptors.
// No libssh blocking mode calls - pure non-blocking via fcntl and ssh_event.
//
// Uses the modern callback-based authentication API (ssh_server_callbacks_struct)
// instead of the deprecated ssh_message_auth_pubkey() functions.

#ifdef HAVE_LIBSSH

#include "ssh_session.h"
#include "sftp_bridge.h"
#include "sftp_path.h"
#include "console.h"
#include "logger.h"
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>
#include <libssh/sftp.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Debug output control - set to true to enable verbose debug output
static constexpr bool DEBUG_SSH = false;
static constexpr bool DEBUG_SFTP = false;

// Get client IP address from ssh_session
static std::string get_client_ip(ssh_session session) {
    socket_t fd = ssh_get_fd(session);
    if (fd == SSH_INVALID_SOCKET) return "unknown";

    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &len) < 0) {
        return "unknown";
    }
    return inet_ntoa(addr.sin_addr);
}


// Set fd to non-blocking at OS level
static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

// Forward declaration for callback
class SSHSession;

// ============================================================================
// Authentication callbacks (modern libssh API)
// ============================================================================

// Called when client attempts "none" authentication (no credentials)
static int auth_none_callback(ssh_session session, const char* user, void* userdata) {
    if (DEBUG_SSH) std::cerr << "[SSH] auth_none_callback called, user=" << (user ? user : "(null)") << "\n";
    SSHSession* ssh_sess = static_cast<SSHSession*>(userdata);
    if (ssh_sess && ssh_sess->server() && ssh_sess->server()->no_auth()) {
        // No authentication required - accept
        if (DEBUG_SSH) std::cerr << "[SSH] auth_none: accepting (no-auth mode)\n";
        std::string ip = get_client_ip(session);
        LOG_SSH(ip, std::string("auth user=") + (user ? user : "") + " method=none");
        ssh_sess->set_authenticated(true);
        return SSH_AUTH_SUCCESS;
    }
    if (DEBUG_SSH) std::cerr << "[SSH] auth_none: denied\n";
    return SSH_AUTH_DENIED;
}

// Called when client attempts public key authentication
// signature_state: SSH_PUBLICKEY_STATE_NONE = probe (is key acceptable?)
//                  SSH_PUBLICKEY_STATE_VALID = signature verified, grant access
static int auth_pubkey_callback(ssh_session session, const char* user,
                                struct ssh_key_struct* pubkey,
                                char signature_state, void* userdata) {
    (void)session;
    (void)user;
    SSHSession* ssh_sess = static_cast<SSHSession*>(userdata);
    if (!ssh_sess || !ssh_sess->server()) {
        return SSH_AUTH_DENIED;
    }

    // No auth mode - accept any key
    if (ssh_sess->server()->no_auth()) {
        if (signature_state == SSH_PUBLICKEY_STATE_VALID) {
            ssh_sess->set_authenticated(true);
            return SSH_AUTH_SUCCESS;
        }
        // Key probe - say it's acceptable
        return SSH_AUTH_SUCCESS;
    }

    // Get key type and base64 blob for comparison
    const char* key_type = ssh_key_type_to_char(ssh_key_type(pubkey));
    char* b64_key = nullptr;
    if (ssh_pki_export_pubkey_base64(pubkey, &b64_key) != SSH_OK || !b64_key) {
        return SSH_AUTH_DENIED;
    }

    std::string key_str = std::string(key_type) + " " + b64_key;
    ssh_string_free_char(b64_key);

    if (!ssh_sess->server()->is_key_authorized(key_str)) {
        // Key not in authorized_keys
        return SSH_AUTH_DENIED;
    }

    // Key is authorized
    if (signature_state == SSH_PUBLICKEY_STATE_VALID) {
        // Signature verified - grant access
        std::string ip = get_client_ip(session);
        LOG_SSH(ip, std::string("auth user=") + (user ? user : "") + " method=publickey");
        ssh_sess->set_authenticated(true);
        return SSH_AUTH_SUCCESS;
    }

    // Key probe - tell client to sign (return success means "try this key")
    return SSH_AUTH_SUCCESS;
}

// Channel callbacks for PTY and shell requests (modern callback-based API)
static int channel_pty_request_callback(ssh_session session, ssh_channel channel,
                                        const char* term, int width, int height,
                                        int pxwidth, int pxheight, void* userdata) {
    (void)session; (void)channel; (void)term;
    (void)width; (void)height; (void)pxwidth; (void)pxheight;
    (void)userdata;
    if (DEBUG_SSH) std::cerr << "[SSH] pty_request_callback invoked\n";
    return 0;  // 0 = accept
}

static int channel_pty_window_change_callback(ssh_session session, ssh_channel channel,
                                               int width, int height,
                                               int pxwidth, int pxheight, void* userdata) {
    (void)session; (void)channel;
    (void)width; (void)height; (void)pxwidth; (void)pxheight;
    (void)userdata;
    // Accept window size changes (we ignore them - CP/M doesn't care)
    if (DEBUG_SSH) std::cerr << "[SSH] window_change_callback: " << width << "x" << height << "\n";
    return 0;  // 0 = accept
}

static int channel_env_request_callback(ssh_session session, ssh_channel channel,
                                         const char* env_name, const char* env_value,
                                         void* userdata) {
    (void)session; (void)channel; (void)userdata;
    // Accept environment variables (we ignore them - CP/M doesn't use them)
    if (DEBUG_SSH) std::cerr << "[SSH] env_request_callback: " << (env_name ? env_name : "")
                             << "=" << (env_value ? env_value : "") << "\n";
    return 0;  // 0 = accept
}

static int channel_shell_request_callback(ssh_session session, ssh_channel channel, void* userdata) {
    if (DEBUG_SSH) std::cerr << "[SSH] shell_request_callback invoked\n";
    SSHSession* ssh_sess = static_cast<SSHSession*>(userdata);
    if (ssh_sess) {
        std::string ip = get_client_ip(session);
        LOG_SSH(ip, "shell");
        ssh_sess->setup_console();
        // Send banner immediately before returning from callback
        // This ensures data is sent before client times out
        ssh_sess->send_banner(channel);
    }
    return 0;  // 0 = accept
}

static int channel_exec_request_callback(ssh_session session, ssh_channel channel,
                                          const char* command, void* userdata) {
    if (DEBUG_SSH) std::cerr << "[SSH] exec_request_callback invoked, command=" << (command ? command : "(null)") << "\n";
    SSHSession* ssh_sess = static_cast<SSHSession*>(userdata);
    if (ssh_sess) {
        std::string ip = get_client_ip(session);
        LOG_SSH(ip, std::string("exec command=") + (command ? command : ""));
        // For exec requests, we still set up the console and let MP/M II handle it
        ssh_sess->setup_console();
        // Send banner immediately
        ssh_sess->send_banner(channel);
    }
    return 0;  // 0 = accept
}

// Called when client requests a subsystem (e.g., "sftp")
static int channel_subsystem_request_callback(ssh_session session, ssh_channel channel,
                                               const char* subsystem, void* userdata) {
    (void)channel;
    SSHSession* ssh_sess = static_cast<SSHSession*>(userdata);

    if (subsystem && strcmp(subsystem, "sftp") == 0) {
        if (DEBUG_SSH) std::cerr << "[SSH] SFTP subsystem requested\n";
        if (ssh_sess) {
            std::string ip = get_client_ip(session);
            LOG_SFTP(ip, "session started");
            // Don't initialize here - the response hasn't been sent yet.
            // Mark as pending and initialize in poll loop after response is sent.
            ssh_sess->mark_sftp_pending();
        }
        return 0;  // 0 = accept
    }

    if (DEBUG_SSH) std::cerr << "[SSH] Unknown subsystem requested: " << (subsystem ? subsystem : "(null)") << "\n";
    return 1;  // non-zero = reject
}

// Called when client opens a channel session
static ssh_channel channel_open_callback(ssh_session session, void* userdata) {
    if (DEBUG_SSH) std::cerr << "[SSH] channel_open_callback invoked\n";
    SSHSession* ssh_sess = static_cast<SSHSession*>(userdata);
    if (ssh_sess) {
        ssh_channel channel = ssh_channel_new(session);
        if (channel) {
            if (DEBUG_SSH) std::cerr << "[SSH] Channel created, setting up callbacks\n";
            // Set up channel callbacks for PTY and shell requests
            ssh_sess->setup_channel_callbacks(channel);
            return channel;
        }
    }
    return nullptr;
}

// ============================================================================
// SSHSession - handles one connection in its own thread
// ============================================================================

SSHSession::SSHSession(ssh_session session, SSHServer* server)
    : session_(session)
    , channel_(nullptr)
    , event_(nullptr)
    , state_(SSHState::KEY_EXCHANGE)
    , console_id_(-1)
    , kex_done_(false)
    , sent_banner_(false)
    , authenticated_(false)
    , server_(server)
    , server_callbacks_{}
    , channel_callbacks_{}
    , client_ip_(get_client_ip(session))
    , sftp_(nullptr)
    , is_sftp_(false)
{
    // Create event context (don't add session until after KEX)
    event_ = ssh_event_new();

    // Use blocking mode - we're in our own thread
    ssh_set_blocking(session_, 1);

    // Log connection
    LOG_SSH(client_ip_, "connected");
}

SSHServer* SSHSession::server() const {
    return server_;
}

void SSHSession::set_authenticated(bool auth) {
    authenticated_ = auth;
    if (auth) {
        state_ = SSHState::CHANNEL_OPEN;
    }
}

void SSHSession::set_channel(ssh_channel channel) {
    channel_ = channel;
}

void SSHSession::setup_channel_callbacks(ssh_channel channel) {
    channel_ = channel;

    // Initialize channel callbacks (modern callback-based API - no ssh_message_get needed)
    memset(&channel_callbacks_, 0, sizeof(channel_callbacks_));
    channel_callbacks_.size = sizeof(channel_callbacks_);
    channel_callbacks_.userdata = this;
    channel_callbacks_.channel_pty_request_function = channel_pty_request_callback;
    channel_callbacks_.channel_pty_window_change_function = channel_pty_window_change_callback;
    channel_callbacks_.channel_env_request_function = channel_env_request_callback;
    channel_callbacks_.channel_shell_request_function = channel_shell_request_callback;
    channel_callbacks_.channel_exec_request_function = channel_exec_request_callback;
    channel_callbacks_.channel_subsystem_request_function = channel_subsystem_request_callback;
    ssh_callbacks_init(&channel_callbacks_);
    ssh_set_channel_callbacks(channel, &channel_callbacks_);
}

void SSHSession::send_banner(ssh_channel channel) {
    if (sent_banner_) return;  // Already sent

    char banner[64];
    snprintf(banner, sizeof(banner), "\r\nMP/M II Console %d\r\n\r\n", console_id_);
    if (DEBUG_SSH) std::cerr << "[SSH] Sending banner to console " << console_id_ << "\n";
    int wr = ssh_channel_write(channel, banner, strlen(banner));
    if (DEBUG_SSH) std::cerr << "[SSH] ssh_channel_write returned " << wr << "\n";
    if (wr < 0) {
        if (DEBUG_SSH) std::cerr << "[SSH] Banner write failed: " << ssh_get_error(session_) << "\n";
        // Don't set state to CLOSED here - let poll_io handle it
    } else {
        sent_banner_ = true;
    }
}

void SSHSession::setup_console() {
    Console* con = ConsoleManager::instance().find_free();
    if (!con) {
        std::cerr << "[SSH] No free console\n";
        state_ = SSHState::CLOSED;
        return;
    }

    console_id_ = con->id();
    con->set_connected(true);
    state_ = SSHState::READY;

    // Clear any stale input from previous session, then inject CR to wake up TMP
    // The CR causes TMP to process an empty command and print a fresh prompt
    // Essential for reconnection after disconnect while TMP was waiting
    con->input_queue().clear();
    con->input_queue().try_write('\r');

    if (DEBUG_SSH) std::cerr << "[SSH] New connection on console " << console_id_ << "\n";
}

void SSHSession::setup_sftp() {
    if (!channel_) {
        if (DEBUG_SFTP) std::cerr << "[SFTP] No channel for SFTP\n";
        state_ = SSHState::CLOSED;
        return;
    }

    if (DEBUG_SFTP) std::cerr << "[SFTP] Creating SFTP server session...\n";

    // Create SFTP server session on this channel
    sftp_ = sftp_server_new(session_, channel_);
    if (!sftp_) {
        if (DEBUG_SFTP) std::cerr << "[SFTP] Failed to create SFTP session: " << ssh_get_error(session_) << "\n";
        state_ = SSHState::CLOSED;
        return;
    }

    if (DEBUG_SFTP) std::cerr << "[SFTP] SFTP session created, initializing protocol...\n";

    // Initialize SFTP protocol (exchanges version info with client)
    // We're already in blocking mode (set in constructor), so this works correctly
    // Note: sftp_server_init is deprecated in libssh master, but the replacement
    // (sftp_channel_default_subsystem_request) isn't available until a future version.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    int rc = sftp_server_init(sftp_);
#pragma GCC diagnostic pop

    if (DEBUG_SFTP) std::cerr << "[SFTP] sftp_server_init returned: " << rc << "\n";

    if (rc != SSH_OK) {
        if (DEBUG_SFTP) std::cerr << "[SFTP] Failed to initialize SFTP: " << ssh_get_error(session_) << "\n";
        sftp_free(sftp_);
        sftp_ = nullptr;
        state_ = SSHState::CLOSED;
        return;
    }

    is_sftp_ = true;
    state_ = SSHState::READY;
    if (DEBUG_SFTP) std::cerr << "[SFTP] SFTP session established\n";
}


SSHSession::~SSHSession() {
    // Request stop and wait for thread to finish
    stop_requested_ = true;
    if (thread_.joinable()) {
        thread_.join();
    }

    // Note: Console is already marked as disconnected in run() before thread exits
    // This happens before running_ is set to false to prevent race conditions

    // Don't call sftp_free() - it sends EOF which can cause "closed by remote host"
    // Let ssh_free() handle cleanup
    // if (sftp_) {
    //     sftp_free(sftp_);
    // }

    if (event_) {
        if (session_ && kex_done_) {
            ssh_event_remove_session(event_, session_);
        }
        ssh_event_free(event_);
    }
    // Don't explicitly free channel - ssh_free() will handle it
    // Calling ssh_channel_free() separately can cause "closed by remote host"
    if (session_) {
        ssh_free(session_);
    }
}

void SSHSession::start() {
    // Set running BEFORE spawning thread to prevent race with poll_sessions()
    running_ = true;
    thread_ = std::thread(&SSHSession::run, this);
}

void SSHSession::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SSHSession::run() {
    // Note: running_ is set in start() before thread spawn to prevent race condition
    if (DEBUG_SSH) std::cerr << "[SSH] Session thread started\n";

    while (!stop_requested_ && state_ != SSHState::CLOSED) {
        // Use ssh_event for polling after key exchange, or add small delay during kex
        if (kex_done_ && event_) {
            // Use short timeout to allow quick response to I/O
            int rc = ssh_event_dopoll(event_, 50);  // 50ms timeout
            // Don't log SSH_AGAIN (-2) spam
            if (DEBUG_SSH && rc != SSH_AGAIN) {
                std::cerr << "[SSH] ssh_event_dopoll returned " << rc
                          << " state=" << (int)state_ << "\n";
            }
            if (rc == SSH_ERROR) {
                if (DEBUG_SSH) std::cerr << "[SSH] ssh_event_dopoll error: " << ssh_get_error(session_) << "\n";
                state_ = SSHState::CLOSED;
                break;
            }
        } else {
            // During key exchange (before event is set up), add small delay
            // to prevent tight-loop spinning that can cause timing issues
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        // Handle SFTP pending state - initialize after subsystem response was sent
        if (state_ == SSHState::SFTP_PENDING) {
            setup_sftp();
            if (state_ == SSHState::CLOSED) break;
            continue;
        }

        if (state_ == SSHState::READY || state_ == SSHState::DRAINING) {
            bool io_result = poll_io();
            if (DEBUG_SSH) std::cerr << "[SSH] poll_io returned " << (io_result ? "true" : "false") << "\n";
            if (!io_result) break;
        } else {
            if (!poll_handshake()) break;
        }
    }

    // Mark console as disconnected BEFORE setting running_ = false
    // This prevents race where new connection tries to use the console
    // before the destructor runs
    if (console_id_ >= 0) {
        Console* con = ConsoleManager::instance().get(console_id_);
        if (con) {
            con->set_connected(false);
        }
    }

    running_ = false;
    if (DEBUG_SSH) {
        std::cerr << "[SSH] Session thread exiting, console " << console_id_
                  << " stop_requested=" << (stop_requested_ ? 1 : 0)
                  << " state=" << (int)state_ << "\n";
    }

    // Log disconnection
    if (is_sftp_) {
        LOG_SFTP(client_ip_, "session ended");
    } else {
        LOG_SSH(client_ip_, "disconnected");
    }
}

bool SSHSession::poll_handshake() {
    switch (state_) {
        case SSHState::KEY_EXCHANGE: {
            int rc = ssh_handle_key_exchange(session_);
            if (DEBUG_SSH) std::cerr << "[SSH] ssh_handle_key_exchange returned: " << rc << "\n";
            if (rc == SSH_OK) {
                if (DEBUG_SSH) std::cerr << "[SSH] KEY_EXCHANGE completed successfully\n";
                kex_done_ = true;

                // Set up server callbacks for authentication (modern API)
                // Also handle channel open via callback for proper integration
                if (DEBUG_SSH) std::cerr << "[SSH] Setting up server callbacks...\n";
                server_callbacks_.size = sizeof(server_callbacks_);
                server_callbacks_.userdata = this;
                server_callbacks_.auth_none_function = auth_none_callback;
                server_callbacks_.auth_pubkey_function = auth_pubkey_callback;
                server_callbacks_.channel_open_request_session_function = channel_open_callback;
                ssh_callbacks_init(&server_callbacks_);
                int cb_rc = ssh_set_server_callbacks(session_, &server_callbacks_);
                if (DEBUG_SSH) std::cerr << "[SSH] ssh_set_server_callbacks returned: " << cb_rc << "\n";

                // Tell client which auth methods we support
                if (server_ && server_->no_auth()) {
                    ssh_set_auth_methods(session_, SSH_AUTH_METHOD_NONE | SSH_AUTH_METHOD_PUBLICKEY);
                } else {
                    ssh_set_auth_methods(session_, SSH_AUTH_METHOD_PUBLICKEY);
                }

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
            // Auth and channel callbacks handle most messages automatically.
            // We only need to drain messages that aren't handled by callbacks
            // to prevent them from piling up.
            // NOTE: Don't call ssh_message_get() for types that callbacks handle,
            // as that would consume the message before the callback sees it.
            // The ssh_event_dopoll() at the top of poll() handles invoking callbacks.
            // We only need to handle unexpected message types here.
            break;
        }

        default:
            break;
    }

    return state_ != SSHState::CLOSED;
}

bool SSHSession::poll_io() {
    if (DEBUG_SSH) std::cerr << "[SSH] poll_io() called, channel_=" << (channel_ ? "valid" : "null") << "\n";
    if (!channel_) {
        if (DEBUG_SSH) std::cerr << "[SSH] poll_io: no channel\n";
        state_ = SSHState::CLOSED;
        return false;
    }

    // Check if channel is still open
    // Note: For SFTP sessions, only check is_closed(), not is_eof()
    // EOF on an SFTP channel doesn't mean the session should end
    if (ssh_channel_is_closed(channel_)) {
        if (DEBUG_SSH) std::cerr << "[SSH] Channel is_closed=1\n";
        state_ = SSHState::CLOSED;
        return false;
    }

    // SFTP sessions have their own polling
    if (is_sftp_) {
        return poll_sftp();
    }

    // For non-SFTP sessions, EOF means disconnect
    if (ssh_channel_is_eof(channel_)) {
        if (DEBUG_SSH) std::cerr << "[SSH] Channel is_eof=1 (non-SFTP)\n";
        state_ = SSHState::CLOSED;
        return false;
    }

    // All channel requests (env, window-change, etc.) are handled by callbacks
    // registered in setup_channel_callbacks() - no ssh_message_get() needed

    Console* con = ConsoleManager::instance().get(console_id_);
    if (!con) {
        if (DEBUG_SSH) std::cerr << "[SSH] poll_io: console " << console_id_ << " not found\n";
        state_ = SSHState::CLOSED;
        return false;
    }

    // Banner is sent in shell_request_callback, but send here as fallback
    if (!sent_banner_) {
        send_banner(channel_);
        if (!sent_banner_) {
            // Banner write failed
            state_ = SSHState::CLOSED;
            return false;
        }
    }

    // Read from SSH -> console input queue (non-blocking)
    char buf[256];
    int n = ssh_channel_read_nonblocking(channel_, buf, sizeof(buf), 0);
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            uint8_t ch = static_cast<uint8_t>(buf[i]);
            con->input_queue().try_write(ch);
        }
    } else if (n == SSH_ERROR) {
        if (DEBUG_SSH) std::cerr << "[SSH] poll_io: ssh_channel_read_nonblocking error: " << ssh_get_error(session_) << "\n";
        state_ = SSHState::CLOSED;
        return false;
    }

    // Write from console output queue -> SSH (batched for efficiency)
    // Check available window size to avoid blocking or data loss
    uint32_t window_size = ssh_channel_window_size(channel_);
    if (window_size > 0) {
        char outbuf[512];
        size_t max_write = (window_size < sizeof(outbuf)) ? window_size : sizeof(outbuf);
        size_t outlen = 0;
        int ch;
        while (outlen < max_write && (ch = con->output_queue().try_read()) >= 0) {
            outbuf[outlen++] = static_cast<char>(ch);
        }
        if (outlen > 0) {
            ssh_channel_write(channel_, outbuf, outlen);
        }
    }

    return true;
}

bool SSHSession::poll_sftp() {
    if (!sftp_) {
        if (DEBUG_SFTP) std::cerr << "[SFTP] No SFTP session\n";
        state_ = SSHState::CLOSED;
        return false;
    }

    // Check if channel is still open
    // Note: Only check is_closed(), not is_eof() - EOF doesn't end SFTP sessions
    if (ssh_channel_is_closed(channel_)) {
        if (DEBUG_SFTP) std::cerr << "[SFTP] Channel closed\n";
        state_ = SSHState::CLOSED;
        return false;
    }

    // Check for pending async operation first
    if (pending_sftp_.msg != nullptr) {
        // Try to get reply (non-blocking)
        auto reply = SftpBridge::instance().try_get_reply(pending_sftp_.request_id);
        if (!reply) {
            // Still waiting - return and let Z80 run
            return true;
        }

        // Got reply - process based on operation type
        if (pending_sftp_.op_type == SSH_FXP_OPENDIR) {
            // Directory enumeration in progress
            auto it = open_dirs_.find(pending_sftp_.handle);
            if (it != open_dirs_.end()) {
                OpenDir* dir = it->second.get();

                // Check if we got entries (status OK and data contains entries)
                // Reply format: [status][count_lo][count_hi][entry0][entry1]...
                // Each entry is 32 bytes
                // Note: more_data flag is already parsed by SftpReply::deserialize
                bool more_data = reply->more_data;

                if (reply->status == SftpReplyStatus::OK && reply->data.size() >= 32) {
                    // Parse all 32-byte CP/M directory entries in this batch
                    size_t offset = 0;
                    while (offset + 32 <= reply->data.size()) {
                        SftpDirEntry entry;
                        std::string name;
                        for (int i = 1; i <= 8; i++) {
                            char c = reply->data[offset + i] & 0x7F;
                            if (c != ' ') name += std::tolower(c);
                        }
                        bool has_ext = false;
                        for (int i = 9; i <= 11; i++) {
                            char c = reply->data[offset + i] & 0x7F;
                            if (c != ' ') {
                                if (!has_ext) { name += '.'; has_ext = true; }
                                name += std::tolower(c);
                            }
                        }
                        entry.name = name;
                        entry.user = reply->data[offset + 0];
                        entry.is_read_only = (reply->data[offset + 9] & 0x80) != 0;
                        entry.is_system = (reply->data[offset + 10] & 0x80) != 0;
                        entry.is_directory = false;
                        uint8_t rc_byte = reply->data[offset + 15];
                        uint8_t ex_byte = reply->data[offset + 12];
                        entry.size = (ex_byte * 128 + rc_byte) * 128;
                        dir->entries.push_back(entry);
                        offset += 32;
                    }

                    if (DEBUG_SFTP) std::cerr << "[SFTP] OPENDIR batch: " << (offset / 32)
                              << " entries, more=" << more_data << "\n";

                    if (more_data) {
                        // Request more entries
                        SftpRequest search_req;
                        search_req.type = SftpRequestType::DIR_SEARCH;
                        search_req.drive = dir->drive;
                        search_req.user = dir->user;
                        search_req.filename = "*.*";
                        search_req.flags = 1;  // Search next
                        pending_sftp_.request_id = SftpBridge::instance().enqueue_request(search_req);
                        pending_sftp_.search_first = false;
                        return true;  // Continue waiting
                    }
                }

                // Enumeration complete (no more data or not found)
                dir->enumeration_complete = true;
                if (DEBUG_SFTP) std::cerr << "[SFTP] OPENDIR complete: " << dir->entries.size() << " entries\n";

                // Create handle data for response
                ssh_string handle_str = ssh_string_new(sizeof(void*));
                ssh_string_fill(handle_str, &pending_sftp_.handle, sizeof(void*));
                sftp_reply_handle(pending_sftp_.msg, handle_str);
                ssh_string_free(handle_str);
                sftp_client_message_free(pending_sftp_.msg);
                pending_sftp_.msg = nullptr;
            }
        } else if (pending_sftp_.op_type == SSH_FXP_STAT) {
            // STAT operation complete
            if (reply->status == SftpReplyStatus::OK && reply->data.size() >= 32) {
                uint8_t rc_byte = reply->data[15];
                uint8_t ex_byte = reply->data[12];
                uint32_t file_size = (ex_byte * 128 + rc_byte) * 128;
                bool read_only = (reply->data[9] & 0x80) != 0;

                struct sftp_attributes_struct attrs = {};
                attrs.permissions = (read_only ? 0444 : 0644) | S_IFREG;
                attrs.size = file_size;
                attrs.flags = SSH_FILEXFER_ATTR_PERMISSIONS | SSH_FILEXFER_ATTR_SIZE;
                sftp_reply_attr(pending_sftp_.msg, &attrs);
            } else {
                sftp_reply_status(pending_sftp_.msg, SSH_FX_NO_SUCH_FILE, "File not found");
            }
            sftp_client_message_free(pending_sftp_.msg);
            pending_sftp_.msg = nullptr;
        }
        return true;
    }

    // Check channel state BEFORE calling sftp_get_client_message
    // which can block/hang if the channel is in a bad state
    if (ssh_channel_is_closed(channel_)) {
        if (DEBUG_SFTP) std::cerr << "[SFTP] Channel closed\n";
        state_ = SSHState::CLOSED;
        return false;
    }

    // Try to get an SFTP client message (non-blocking)
    sftp_client_message msg = sftp_get_client_message(sftp_);
    if (!msg) {
        // No message - check if client has disconnected
        if (ssh_channel_is_eof(channel_)) {
            if (DEBUG_SFTP) std::cerr << "[SFTP] No more messages, EOF detected\n";
            // Just mark as closed - sftp_free() in destructor will handle EOF/cleanup
            state_ = SSHState::CLOSED;
            return false;
        }
        // No message available
        return true;
    }

    // Get message type and filename
    uint8_t type = sftp_client_message_get_type(msg);
    const char* filename = sftp_client_message_get_filename(msg);

    if (DEBUG_SFTP) {
        std::cerr << "[SFTP] Message type " << (int)type;
        if (filename) {
            std::cerr << " path: " << filename;
        }
        std::cerr << "\n";
    }

    int rc = 0;
    switch (type) {
        case SSH_FXP_REALPATH: {
            // Resolve path - normalize and return canonical form
            std::string path = filename ? filename : "/";
            SftpPath parsed = parse_sftp_path(path);
            std::string resolved = sftp_path_to_string(parsed);

            struct sftp_attributes_struct attrs = {};
            attrs.permissions = 0755 | S_IFDIR;  // Directory
            attrs.uid = 0;
            attrs.gid = 0;
            attrs.size = 0;
            attrs.flags = SSH_FILEXFER_ATTR_PERMISSIONS;

            if (DEBUG_SFTP) std::cerr << "[SFTP] REALPATH: " << path << " -> " << resolved << "\n";
            rc = sftp_reply_name(msg, resolved.c_str(), &attrs);
            break;
        }

        case SSH_FXP_STAT:
        case SSH_FXP_LSTAT: {
            std::string path = filename ? filename : "/";
            SftpPath parsed = parse_sftp_path(path);

            struct sftp_attributes_struct attrs = {};
            attrs.uid = 0;
            attrs.gid = 0;
            attrs.flags = SSH_FILEXFER_ATTR_PERMISSIONS | SSH_FILEXFER_ATTR_SIZE;

            if (parsed.is_root()) {
                // Root directory
                attrs.permissions = 0755 | S_IFDIR;
                attrs.size = 0;
                rc = sftp_reply_attr(msg, &attrs);
            } else if (parsed.is_drive_root() || parsed.is_user_dir()) {
                // Drive or user directory
                attrs.permissions = 0755 | S_IFDIR;
                attrs.size = 0;
                rc = sftp_reply_attr(msg, &attrs);
            } else if (parsed.is_file()) {
                // Check if file exists via RSP bridge (async)
                if (DEBUG_SFTP) std::cerr << "[SFTP] STAT: looking for file '" << parsed.filename
                          << "' drive=" << parsed.drive << " user=" << parsed.user << std::endl;

                SftpRequest search_req;
                search_req.type = SftpRequestType::DIR_SEARCH;
                search_req.drive = parsed.drive;
                search_req.user = (parsed.user >= 0) ? parsed.user : 0;
                search_req.filename = parsed.filename;
                search_req.flags = 0;  // Search first

                // Start async operation
                pending_sftp_.msg = msg;
                pending_sftp_.request_id = SftpBridge::instance().enqueue_request(search_req);
                pending_sftp_.op_type = SSH_FXP_STAT;
                return true;  // Will reply when RSP responds
            } else {
                rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "Path not found");
            }
            break;
        }

        case SSH_FXP_OPENDIR: {
            std::string path = filename ? filename : "/";
            SftpPath parsed = parse_sftp_path(path);

            if (DEBUG_SFTP) std::cerr << "[SFTP] OPENDIR: drive=" << parsed.drive
                      << " user=" << parsed.user << "\n";

            // Create directory listing
            auto dir = std::make_unique<OpenDir>();
            dir->drive = parsed.drive;
            dir->user = parsed.user;
            dir->index = 0;

            if (parsed.is_root()) {
                // List mounted drives as directories
                auto drives = get_mounted_drives();
                for (int d : drives) {
                    SftpDirEntry entry;
                    entry.name = std::string(1, 'A' + d);
                    entry.is_directory = true;
                    entry.size = 0;
                    entry.user = 0;
                    entry.is_system = false;
                    entry.is_read_only = false;
                    dir->entries.push_back(entry);
                }
            } else if (parsed.drive >= 0) {
                // List files on drive/user via RSP bridge (async)
                int user = (parsed.user >= 0) ? parsed.user : 0;
                dir->user = user;

                // Create handle now (we'll enumerate async)
                void* handle = reinterpret_cast<void*>(next_handle_id_++);
                open_dirs_[handle] = std::move(dir);

                // Start async directory enumeration
                SftpRequest search_req;
                search_req.type = SftpRequestType::DIR_SEARCH;
                search_req.drive = parsed.drive;
                search_req.user = user;
                search_req.filename = "*.*";
                search_req.flags = 0;  // Search first

                pending_sftp_.msg = msg;
                pending_sftp_.request_id = SftpBridge::instance().enqueue_request(search_req);
                pending_sftp_.op_type = SSH_FXP_OPENDIR;
                pending_sftp_.handle = handle;
                pending_sftp_.search_first = true;
                return true;  // Will reply when enumeration completes
            }

            // Root directory - complete immediately
            if (DEBUG_SFTP) std::cerr << "[SFTP] OPENDIR: " << dir->entries.size() << " entries\n";

            // Create handle (use pointer as handle)
            void* handle = reinterpret_cast<void*>(next_handle_id_++);
            open_dirs_[handle] = std::move(dir);

            // Create handle data for response
            ssh_string handle_str = ssh_string_new(sizeof(void*));
            ssh_string_fill(handle_str, &handle, sizeof(void*));
            rc = sftp_reply_handle(msg, handle_str);
            ssh_string_free(handle_str);
            break;
        }

        case SSH_FXP_READDIR: {
            ssh_string handle_str = msg->handle;
            if (!handle_str || ssh_string_len(handle_str) != sizeof(void*)) {
                rc = sftp_reply_status(msg, SSH_FX_BAD_MESSAGE, "Invalid handle");
                break;
            }

            void* handle;
            std::memcpy(&handle, ssh_string_data(handle_str), sizeof(void*));

            auto it = open_dirs_.find(handle);
            if (it == open_dirs_.end()) {
                rc = sftp_reply_status(msg, SSH_FX_BAD_MESSAGE, "Invalid handle");
                break;
            }

            OpenDir* dir = it->second.get();

            if (dir->index >= dir->entries.size()) {
                // No more entries
                rc = sftp_reply_status(msg, SSH_FX_EOF, "End of directory");
                break;
            }

            // Return entries (up to 20 at a time)
            int count = 0;

            while (dir->index < dir->entries.size() && count < 20) {
                const SftpDirEntry& entry = dir->entries[dir->index++];

                struct sftp_attributes_struct attrs = {};
                attrs.uid = entry.user;
                attrs.gid = 0;
                attrs.size = entry.size;
                attrs.flags = SSH_FILEXFER_ATTR_PERMISSIONS | SSH_FILEXFER_ATTR_SIZE;

                if (entry.is_directory) {
                    attrs.permissions = 0755 | S_IFDIR;
                } else {
                    attrs.permissions = entry.is_read_only ? 0444 : 0644;
                }

                // Convert filename to lowercase for display
                std::string lower_name = entry.name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                // Long name format: simpler format
                char longname[128];
                snprintf(longname, sizeof(longname), "%s", lower_name.c_str());

                rc = sftp_reply_names_add(msg, lower_name.c_str(), longname, &attrs);
                if (rc != 0) {
                    if (DEBUG_SFTP) std::cerr << "[SFTP] sftp_reply_names_add failed: " << rc << "\n";
                    break;
                }
                count++;
            }

            if (count > 0) {
                rc = sftp_reply_names(msg);
            } else {
                rc = sftp_reply_status(msg, SSH_FX_EOF, "End of directory");
            }
            if (DEBUG_SFTP) std::cerr << "[SFTP] READDIR: returned " << count << " entries\n";
            break;
        }

        case SSH_FXP_CLOSE: {
            ssh_string handle_str = msg->handle;
            if (handle_str && ssh_string_len(handle_str) == sizeof(void*)) {
                void* handle;
                std::memcpy(&handle, ssh_string_data(handle_str), sizeof(void*));

                // Try to close as directory first
                if (open_dirs_.erase(handle) > 0) {
                    rc = sftp_reply_status(msg, SSH_FX_OK, "OK");
                    break;
                }

                // Try to close as file
                auto it = open_files_.find(handle);
                if (it != open_files_.end()) {
                    OpenFile* file = it->second.get();

                    // If file was opened for writing, flush data to disk
                    if (file->is_write && !file->cached_data.empty()) {
                        if (DEBUG_SFTP) std::cerr << "[SFTP] CLOSE: writing " << file->cached_data.size()
                                  << " bytes to disk\n";

                        // Write data in chunks (max 1920 bytes per RSP request)
                        constexpr size_t CHUNK_SIZE = 1920;
                        size_t offset = 0;
                        bool write_error = false;

                        while (offset < file->cached_data.size() && !write_error) {
                            size_t chunk_len = std::min(CHUNK_SIZE, file->cached_data.size() - offset);

                            SftpRequest write_req;
                            write_req.type = SftpRequestType::FILE_WRITE;
                            write_req.drive = file->drive;
                            write_req.user = file->user;
                            write_req.filename = file->filename;
                            write_req.offset = offset;
                            write_req.length = chunk_len;
                            write_req.data.assign(
                                file->cached_data.begin() + offset,
                                file->cached_data.begin() + offset + chunk_len);

                            uint32_t req_id = SftpBridge::instance().enqueue_request(write_req);
                            auto write_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);

                            if (!write_reply || write_reply->status != SftpReplyStatus::OK) {
                                if (DEBUG_SFTP) std::cerr << "[SFTP] CLOSE: write error at offset " << offset << "\n";
                                write_error = true;
                            } else {
                                offset += chunk_len;
                                if (DEBUG_SFTP) std::cerr << "[SFTP] CLOSE: wrote " << chunk_len
                                          << " bytes, total=" << offset << "\n";
                            }
                        }

                        // Close file via RSP
                        SftpRequest close_req;
                        close_req.type = SftpRequestType::FILE_CLOSE;
                        close_req.drive = file->drive;
                        close_req.user = file->user;
                        close_req.filename = file->filename;
                        SftpBridge::instance().enqueue_request(close_req);
                        // Don't wait for close reply

                        if (write_error) {
                            open_files_.erase(it);
                            rc = sftp_reply_status(msg, SSH_FX_FAILURE, "Write failed");
                            break;
                        }
                    }

                    open_files_.erase(it);
                }
            }
            rc = sftp_reply_status(msg, SSH_FX_OK, "OK");
            break;
        }

        case SSH_FXP_OPEN: {
            std::string path = filename ? filename : "";
            SftpPath parsed = parse_sftp_path(path);

            // Check open flags
            uint32_t flags = msg->flags;
            bool want_read = (flags & SSH_FXF_READ) != 0;
            bool want_write = (flags & SSH_FXF_WRITE) != 0;
            bool want_creat = (flags & SSH_FXF_CREAT) != 0;
            bool want_trunc = (flags & SSH_FXF_TRUNC) != 0;

            if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: " << path << " drive=" << parsed.drive
                      << " user=" << parsed.user << " file=" << parsed.filename
                      << " flags=" << flags << " (R=" << want_read << " W=" << want_write
                      << " C=" << want_creat << " T=" << want_trunc << ")\n";

            if (!parsed.is_file()) {
                rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "Not a file");
                break;
            }

            if (want_write) {
                // Write mode - create or truncate file
                bool file_exists = false;

                // Check if file exists via DIR_SEARCH
                SftpRequest search_req;
                search_req.type = SftpRequestType::DIR_SEARCH;
                search_req.drive = parsed.drive;
                search_req.user = parsed.user;
                search_req.filename = parsed.filename;
                search_req.flags = 0;  // Search first

                uint32_t req_id = SftpBridge::instance().enqueue_request(search_req);
                auto search_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);
                if (search_reply && search_reply->status == SftpReplyStatus::OK) {
                    file_exists = true;
                }

                // If file exists and we want to truncate, delete it first
                if (file_exists && want_trunc) {
                    if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: deleting existing file for truncate\n";
                    SftpRequest del_req;
                    del_req.type = SftpRequestType::FILE_DELETE;
                    del_req.drive = parsed.drive;
                    del_req.user = parsed.user;
                    del_req.filename = parsed.filename;

                    req_id = SftpBridge::instance().enqueue_request(del_req);
                    auto del_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);
                    if (!del_reply || del_reply->status != SftpReplyStatus::OK) {
                        if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: failed to delete for truncate\n";
                    }
                    file_exists = false;
                }

                // If file doesn't exist and we want to create, create it
                if (!file_exists && want_creat) {
                    if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: creating new file\n";
                    SftpRequest create_req;
                    create_req.type = SftpRequestType::FILE_CREATE;
                    create_req.drive = parsed.drive;
                    create_req.user = parsed.user;
                    create_req.filename = parsed.filename;

                    req_id = SftpBridge::instance().enqueue_request(create_req);
                    auto create_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);
                    if (!create_reply || create_reply->status != SftpReplyStatus::OK) {
                        SftpReplyStatus st = create_reply ? create_reply->status : SftpReplyStatus::ERROR_INVALID;
                        if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: create failed, status=" << (int)st << "\n";
                        if (st == SftpReplyStatus::ERROR_DISK_FULL) {
                            rc = sftp_reply_status(msg, SSH_FX_FAILURE, "Disk full");
                        } else {
                            rc = sftp_reply_status(msg, SSH_FX_FAILURE, "Cannot create file");
                        }
                        break;
                    }
                    file_exists = true;
                }

                if (!file_exists) {
                    rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "File not found");
                    break;
                }

                // Open file via RSP bridge
                SftpRequest open_req;
                open_req.type = SftpRequestType::FILE_OPEN;
                open_req.drive = parsed.drive;
                open_req.user = parsed.user;
                open_req.filename = parsed.filename;
                open_req.flags = 1;  // Write mode

                req_id = SftpBridge::instance().enqueue_request(open_req);
                auto open_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);
                if (!open_reply || open_reply->status != SftpReplyStatus::OK) {
                    if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: file open failed for write\n";
                    rc = sftp_reply_status(msg, SSH_FX_FAILURE, "Cannot open file");
                    break;
                }

                // Create file handle for writing
                auto file = std::make_unique<OpenFile>();
                file->drive = parsed.drive;
                file->user = parsed.user;
                file->filename = parsed.filename;
                file->size = 0;
                file->offset = 0;
                file->is_read_only = false;
                file->is_write = true;
                file->file_created = true;
                // cached_data will accumulate written data

                void* handle = reinterpret_cast<void*>(next_handle_id_++);
                open_files_[handle] = std::move(file);

                if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: write mode, file ready\n";

                ssh_string handle_str = ssh_string_new(sizeof(void*));
                ssh_string_fill(handle_str, &handle, sizeof(void*));
                rc = sftp_reply_handle(msg, handle_str);
                ssh_string_free(handle_str);
                break;
            }

            // Read mode - open existing file and cache contents
            SftpRequest open_req;
            open_req.type = SftpRequestType::FILE_OPEN;
            open_req.drive = parsed.drive;
            open_req.user = parsed.user;
            open_req.filename = parsed.filename;
            open_req.flags = 0;  // Read mode

            uint32_t req_id = SftpBridge::instance().enqueue_request(open_req);
            auto open_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);

            if (!open_reply || open_reply->status != SftpReplyStatus::OK) {
                if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: file not found via RSP\n";
                rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "File not found");
                break;
            }

            // Read entire file via RSP bridge (batched: up to 1920 bytes at a time)
            std::vector<uint8_t> file_data;
            bool read_error = false;
            bool more_data = true;
            while (more_data) {
                SftpRequest read_req;
                read_req.type = SftpRequestType::FILE_READ;
                read_req.drive = parsed.drive;
                read_req.user = parsed.user;
                read_req.filename = parsed.filename;

                req_id = SftpBridge::instance().enqueue_request(read_req);
                auto read_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);

                if (!read_reply) {
                    if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: read timeout\n";
                    read_error = true;
                    break;
                }

                // Check status and MORE_DATA flag (already parsed by deserialize)
                more_data = read_reply->more_data;

                if (read_reply->status == SftpReplyStatus::ERROR_NOT_FOUND) {
                    // EOF reached (no data in first read = file not found/empty)
                    break;
                }

                if (read_reply->status != SftpReplyStatus::OK) {
                    if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: read error status=" << (int)read_reply->status << "\n";
                    read_error = true;
                    break;
                }

                // Append data (batched read returns up to 1920 bytes)
                file_data.insert(file_data.end(),
                                 read_reply->data.begin(),
                                 read_reply->data.end());

                if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: read " << read_reply->data.size()
                          << " bytes, total=" << file_data.size()
                          << ", more=" << more_data << "\n";
            }

            // Close file via RSP
            SftpRequest close_req;
            close_req.type = SftpRequestType::FILE_CLOSE;
            close_req.drive = parsed.drive;
            close_req.user = parsed.user;
            close_req.filename = parsed.filename;
            SftpBridge::instance().enqueue_request(close_req);
            // Don't wait for close reply

            if (read_error) {
                rc = sftp_reply_status(msg, SSH_FX_FAILURE, "File read error");
                break;
            }

            // Create file handle with cached data
            auto file = std::make_unique<OpenFile>();
            file->drive = parsed.drive;
            file->user = parsed.user;
            file->filename = parsed.filename;
            file->size = file_data.size();
            file->offset = 0;
            file->is_read_only = true;
            file->is_write = false;
            file->file_created = false;
            file->cached_data = std::move(file_data);

            void* handle = reinterpret_cast<void*>(next_handle_id_++);
            open_files_[handle] = std::move(file);

            if (DEBUG_SFTP) std::cerr << "[SFTP] OPEN: success, cached " << open_files_[handle]->size << " bytes\n";

            ssh_string handle_str = ssh_string_new(sizeof(void*));
            ssh_string_fill(handle_str, &handle, sizeof(void*));
            rc = sftp_reply_handle(msg, handle_str);
            ssh_string_free(handle_str);
            break;
        }

        case SSH_FXP_READ: {
            ssh_string handle_str = msg->handle;
            if (!handle_str || ssh_string_len(handle_str) != sizeof(void*)) {
                rc = sftp_reply_status(msg, SSH_FX_BAD_MESSAGE, "Invalid handle");
                break;
            }

            void* handle;
            std::memcpy(&handle, ssh_string_data(handle_str), sizeof(void*));

            auto it = open_files_.find(handle);
            if (it == open_files_.end()) {
                rc = sftp_reply_status(msg, SSH_FX_BAD_MESSAGE, "Invalid file handle");
                break;
            }

            OpenFile* file = it->second.get();
            uint64_t offset = msg->offset;
            uint32_t len = msg->len;

            if (DEBUG_SFTP) std::cerr << "[SFTP] READ: offset=" << offset << " len=" << len
                      << " cached_size=" << file->cached_data.size() << "\n";

            if (offset >= file->cached_data.size()) {
                rc = sftp_reply_status(msg, SSH_FX_EOF, "End of file");
                break;
            }

            // Return data from cache
            uint32_t available = file->cached_data.size() - offset;
            uint32_t to_read = std::min(len, available);

            if (DEBUG_SFTP) std::cerr << "[SFTP] READ: returning " << to_read << " bytes from cache\n";
            rc = sftp_reply_data(msg, file->cached_data.data() + offset, to_read);
            break;
        }

        case SSH_FXP_WRITE: {
            ssh_string handle_str = msg->handle;
            if (!handle_str || ssh_string_len(handle_str) != sizeof(void*)) {
                rc = sftp_reply_status(msg, SSH_FX_BAD_MESSAGE, "Invalid handle");
                break;
            }

            void* handle;
            std::memcpy(&handle, ssh_string_data(handle_str), sizeof(void*));

            auto it = open_files_.find(handle);
            if (it == open_files_.end()) {
                rc = sftp_reply_status(msg, SSH_FX_BAD_MESSAGE, "Invalid file handle");
                break;
            }

            OpenFile* file = it->second.get();
            if (!file->is_write) {
                rc = sftp_reply_status(msg, SSH_FX_PERMISSION_DENIED, "File not opened for writing");
                break;
            }

            uint64_t offset = msg->offset;
            ssh_string data_str = msg->data;
            if (!data_str) {
                rc = sftp_reply_status(msg, SSH_FX_BAD_MESSAGE, "No data");
                break;
            }

            size_t data_len = ssh_string_len(data_str);
            const uint8_t* data_ptr = reinterpret_cast<const uint8_t*>(ssh_string_data(data_str));

            if (DEBUG_SFTP) std::cerr << "[SFTP] WRITE: offset=" << offset << " len=" << data_len << "\n";

            // Expand cached_data if needed and write at offset
            if (offset + data_len > file->cached_data.size()) {
                file->cached_data.resize(offset + data_len);
            }
            std::memcpy(file->cached_data.data() + offset, data_ptr, data_len);
            file->size = file->cached_data.size();

            rc = sftp_reply_status(msg, SSH_FX_OK, "OK");
            break;
        }

        case SSH_FXP_REMOVE: {
            std::string path = filename ? filename : "";
            SftpPath parsed = parse_sftp_path(path);

            if (DEBUG_SFTP) std::cerr << "[SFTP] REMOVE: " << path << " drive=" << parsed.drive
                      << " user=" << parsed.user << " file=" << parsed.filename << "\n";

            if (!parsed.is_file()) {
                rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "Not a file");
                break;
            }

            // Delete file via RSP bridge
            SftpRequest del_req;
            del_req.type = SftpRequestType::FILE_DELETE;
            del_req.drive = parsed.drive;
            del_req.user = parsed.user;
            del_req.filename = parsed.filename;

            uint32_t req_id = SftpBridge::instance().enqueue_request(del_req);
            auto del_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);

            if (!del_reply || del_reply->status != SftpReplyStatus::OK) {
                if (DEBUG_SFTP) std::cerr << "[SFTP] REMOVE: file not found\n";
                rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "File not found");
                break;
            }

            if (DEBUG_SFTP) std::cerr << "[SFTP] REMOVE: success\n";
            rc = sftp_reply_status(msg, SSH_FX_OK, "OK");
            break;
        }

        case SSH_FXP_RENAME: {
            std::string old_path = filename ? filename : "";
            const char* new_path_cstr = sftp_client_message_get_data(msg);
            std::string new_path = new_path_cstr ? new_path_cstr : "";

            SftpPath old_parsed = parse_sftp_path(old_path);
            SftpPath new_parsed = parse_sftp_path(new_path);

            if (DEBUG_SFTP) std::cerr << "[SFTP] RENAME: " << old_path << " -> " << new_path
                      << " (drive=" << old_parsed.drive << " user=" << old_parsed.user
                      << " old=" << old_parsed.filename << " new=" << new_parsed.filename << ")\n";

            if (!old_parsed.is_file() || !new_parsed.is_file()) {
                rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "Not a file");
                break;
            }

            // Must be same drive and user for CP/M rename
            if (old_parsed.drive != new_parsed.drive || old_parsed.user != new_parsed.user) {
                rc = sftp_reply_status(msg, SSH_FX_OP_UNSUPPORTED, "Cannot rename across drives/users");
                break;
            }

            // Rename file via RSP bridge
            SftpRequest ren_req;
            ren_req.type = SftpRequestType::FILE_RENAME;
            ren_req.drive = old_parsed.drive;
            ren_req.user = old_parsed.user;
            ren_req.filename = old_parsed.filename;
            ren_req.new_filename = new_parsed.filename;

            uint32_t req_id = SftpBridge::instance().enqueue_request(ren_req);
            auto ren_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);

            if (!ren_reply || ren_reply->status != SftpReplyStatus::OK) {
                if (DEBUG_SFTP) std::cerr << "[SFTP] RENAME: failed\n";
                rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "Rename failed");
                break;
            }

            if (DEBUG_SFTP) std::cerr << "[SFTP] RENAME: success\n";
            rc = sftp_reply_status(msg, SSH_FX_OK, "OK");
            break;
        }

        case SSH_FXP_EXTENDED: {
            // Handle extended operations (e.g., posix-rename@openssh.com)
            const char* submessage = sftp_client_message_get_submessage(msg);
            std::string ext_name = submessage ? submessage : "";
            if (DEBUG_SFTP) std::cerr << "[SFTP] EXTENDED: " << ext_name << "\n";

            if (ext_name == "posix-rename@openssh.com") {
                // posix-rename: oldpath is filename, newpath is in data
                std::string old_path = filename ? filename : "";
                const char* new_path_cstr = sftp_client_message_get_data(msg);
                std::string new_path = new_path_cstr ? new_path_cstr : "";

                SftpPath old_parsed = parse_sftp_path(old_path);
                SftpPath new_parsed = parse_sftp_path(new_path);

                if (DEBUG_SFTP) std::cerr << "[SFTP] POSIX-RENAME: " << old_path << " -> " << new_path
                          << " (drive=" << old_parsed.drive << " user=" << old_parsed.user
                          << " old=" << old_parsed.filename << " new=" << new_parsed.filename << ")\n";

                if (!old_parsed.is_file() || !new_parsed.is_file()) {
                    rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "Not a file");
                    break;
                }

                // Must be same drive and user for CP/M rename
                if (old_parsed.drive != new_parsed.drive || old_parsed.user != new_parsed.user) {
                    rc = sftp_reply_status(msg, SSH_FX_OP_UNSUPPORTED, "Cannot rename across drives/users");
                    break;
                }

                // Rename file via RSP bridge
                SftpRequest ren_req;
                ren_req.type = SftpRequestType::FILE_RENAME;
                ren_req.drive = old_parsed.drive;
                ren_req.user = old_parsed.user;
                ren_req.filename = old_parsed.filename;
                ren_req.new_filename = new_parsed.filename;

                uint32_t req_id = SftpBridge::instance().enqueue_request(ren_req);
                auto ren_reply = SftpBridge::instance().wait_for_reply(req_id, 10000);

                if (!ren_reply || ren_reply->status != SftpReplyStatus::OK) {
                    if (DEBUG_SFTP) std::cerr << "[SFTP] POSIX-RENAME: failed\n";
                    rc = sftp_reply_status(msg, SSH_FX_NO_SUCH_FILE, "Rename failed");
                    break;
                }

                if (DEBUG_SFTP) std::cerr << "[SFTP] POSIX-RENAME: success\n";
                rc = sftp_reply_status(msg, SSH_FX_OK, "OK");
            } else {
                if (DEBUG_SFTP) std::cerr << "[SFTP] Unknown extended operation: " << ext_name << "\n";
                rc = sftp_reply_status(msg, SSH_FX_OP_UNSUPPORTED, "Unknown extended operation");
            }
            break;
        }

        default:
            if (DEBUG_SFTP) std::cerr << "[SFTP] Unsupported operation: " << (int)type << "\n";
            rc = sftp_reply_status(msg, SSH_FX_OP_UNSUPPORTED, "Operation not supported");
            break;
    }

    if (rc != 0) {
        if (DEBUG_SFTP) std::cerr << "[SFTP] Reply failed with rc=" << rc << "\n";
    }

    sftp_client_message_free(msg);
    return true;
}

// ============================================================================
// SSHServer - accepts connections with non-blocking polling
// ============================================================================

SSHServer::SSHServer()
    : running_(false)
{
}

SSHServer::~SSHServer() {
    stop();
}

bool SSHServer::load_authorized_keys(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[SSH] Cannot open authorized_keys: " << path << "\n";
        return false;
    }

    authorized_keys_.clear();
    std::string line;
    int count = 0;

    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Parse: key_type base64_key [comment]
        // We store the "key_type base64_key" portion for comparison
        std::istringstream iss(line);
        std::string key_type, key_blob;
        if (iss >> key_type >> key_blob) {
            // Store as "type blob" for matching
            authorized_keys_.push_back(key_type + " " + key_blob);
            count++;
        }
    }

    if (DEBUG_SSH) std::cerr << "[SSH] Loaded " << count << " authorized keys from " << path << "\n";
    return count > 0;
}

bool SSHServer::is_key_authorized(const std::string& key_blob) const {
    for (const auto& key : authorized_keys_) {
        if (key == key_blob) {
            return true;
        }
    }
    return false;
}

bool SSHServer::init(const std::string& host_key_path) {
    // Just store the host key path - we'll use it when creating listeners
    host_key_path_ = host_key_path;
    return true;
}

bool SSHServer::listen(const std::string& host, int port) {
    if (host_key_path_.empty()) {
        std::cerr << "SSH server not initialized (call init() first)\n";
        return false;
    }

    ListenAddress addr(host, port);

    // Create a new ssh_bind for this listener
    ssh_bind sshbind = ssh_bind_new();
    if (!sshbind) {
        std::cerr << "Failed to create SSH bind for " << addr.to_string() << "\n";
        return false;
    }

    // Set host key
    if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_HOSTKEY, host_key_path_.c_str()) < 0) {
        std::cerr << "Failed to set host key: " << ssh_get_error(sshbind) << "\n";
        ssh_bind_free(sshbind);
        return false;
    }

    // Set bind address if specified
    if (!host.empty()) {
        if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDADDR, host.c_str()) < 0) {
            std::cerr << "Failed to set SSH bind address: " << ssh_get_error(sshbind) << "\n";
            ssh_bind_free(sshbind);
            return false;
        }
    }

    if (ssh_bind_options_set(sshbind, SSH_BIND_OPTIONS_BINDPORT, &port) < 0) {
        std::cerr << "Failed to set SSH port: " << ssh_get_error(sshbind) << "\n";
        ssh_bind_free(sshbind);
        return false;
    }

    if (ssh_bind_listen(sshbind) < 0) {
        std::cerr << "Failed to bind SSH server to " << addr.to_string() << ": " << ssh_get_error(sshbind) << "\n";
        ssh_bind_free(sshbind);
        return false;
    }

    // Set bind socket non-blocking at OS level
    socket_t bind_fd = ssh_bind_get_fd(sshbind);
    if (bind_fd != SSH_INVALID_SOCKET) {
        set_nonblocking(bind_fd);
    }

    listeners_.push_back({sshbind, addr});
    listen_addrs_.push_back(addr);
    running_ = true;
    return true;
}

void SSHServer::stop() {
    running_ = false;
    sessions_.clear();

    for (auto& listener : listeners_) {
        if (listener.bind) {
            ssh_bind_free(listener.bind);
        }
    }
    listeners_.clear();
    listen_addrs_.clear();
}

void SSHServer::poll() {
    if (!running_) return;

    poll_accept();
    poll_sessions();
}

void SSHServer::poll_accept() {
    // Check all listeners for pending connections
    for (auto& listener : listeners_) {
        // Check if a connection is pending before calling accept
        // This avoids EAGAIN spam on macOS with non-blocking sockets
        socket_t bind_fd = ssh_bind_get_fd(listener.bind);
        if (bind_fd == SSH_INVALID_SOCKET) continue;

        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(bind_fd, &readfds);

        struct timeval tv = {0, 0};  // No wait - just check
        int ready = select(bind_fd + 1, &readfds, nullptr, nullptr, &tv);
        if (ready <= 0) {
            // No connection pending (ready == 0) or error (ready < 0)
            continue;
        }

        // Connection is pending - try to accept
        ssh_session session = ssh_new();
        if (!session) continue;

        int rc = ssh_bind_accept(listener.bind, session);
        if (rc != SSH_OK) {
            // Unexpected error since select() said a connection was ready
            std::cerr << "[SSH] accept failed: " << ssh_get_error(listener.bind) << "\n";
            ssh_free(session);
            continue;
        }

        if (DEBUG_SSH) std::cerr << "[SSH] New connection accepted on " << listener.addr.to_string() << "!\n" << std::flush;

        // Create session object and start its thread
        // Session constructor sets blocking mode; session runs in its own thread
        auto ssh_session_ptr = std::make_unique<SSHSession>(session, this);
        ssh_session_ptr->start();
        sessions_.push_back(std::move(ssh_session_ptr));
        if (DEBUG_SSH) std::cerr << "[SSH] Session thread started, total sessions: " << sessions_.size() << "\n";
    }
}

void SSHServer::poll_sessions() {
    // Remove finished sessions (their threads have exited)
    sessions_.erase(
        std::remove_if(sessions_.begin(), sessions_.end(),
                       [](std::unique_ptr<SSHSession>& s) {
                           return !s->is_running();
                       }),
        sessions_.end());
}

size_t SSHServer::session_count() const {
    return sessions_.size();
}

#endif // HAVE_LIBSSH
