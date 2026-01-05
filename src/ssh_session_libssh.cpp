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
#include "console.h"
#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

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
    (void)session;
    (void)user;
    SSHSession* ssh_sess = static_cast<SSHSession*>(userdata);
    if (ssh_sess && ssh_sess->server() && ssh_sess->server()->no_auth()) {
        // No authentication required - accept
        ssh_sess->set_authenticated(true);
        return SSH_AUTH_SUCCESS;
    }
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
        ssh_sess->set_authenticated(true);
        return SSH_AUTH_SUCCESS;
    }

    // Key probe - tell client to sign (return success means "try this key")
    return SSH_AUTH_SUCCESS;
}

// Channel callbacks for PTY and shell requests
static int channel_pty_request_callback(ssh_session session, ssh_channel channel,
                                        const char* term, int width, int height,
                                        int pxwidth, int pxheight, void* userdata) {
    (void)session; (void)channel; (void)term;
    (void)width; (void)height; (void)pxwidth; (void)pxheight;
    (void)userdata;
    return 0;  // 0 = accept
}

static int channel_shell_request_callback(ssh_session session, ssh_channel channel, void* userdata) {
    (void)session; (void)channel;
    SSHSession* ssh_sess = static_cast<SSHSession*>(userdata);
    if (ssh_sess) {
        ssh_sess->setup_console();
    }
    return 0;  // 0 = accept
}

// Called when client opens a channel session
static ssh_channel channel_open_callback(ssh_session session, void* userdata) {
    SSHSession* ssh_sess = static_cast<SSHSession*>(userdata);
    if (ssh_sess) {
        ssh_channel channel = ssh_channel_new(session);
        if (channel) {
            // Set up channel callbacks for PTY and shell requests
            ssh_sess->setup_channel_callbacks(channel);
            return channel;
        }
    }
    return nullptr;
}

// ============================================================================
// SSHSession - handles one connection with non-blocking I/O
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
{
    // Create event context (don't add session until after KEX)
    event_ = ssh_event_new();
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

    // Initialize channel callbacks
    memset(&channel_callbacks_, 0, sizeof(channel_callbacks_));
    channel_callbacks_.size = sizeof(channel_callbacks_);
    channel_callbacks_.userdata = this;
    channel_callbacks_.channel_pty_request_function = channel_pty_request_callback;
    channel_callbacks_.channel_shell_request_function = channel_shell_request_callback;
    ssh_callbacks_init(&channel_callbacks_);
    ssh_set_channel_callbacks(channel, &channel_callbacks_);
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

    std::cerr << "[SSH] New connection on console " << console_id_ << "\n";
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

                // Set up server callbacks for authentication (modern API)
                // Also handle channel open via callback for proper integration
                server_callbacks_.size = sizeof(server_callbacks_);
                server_callbacks_.userdata = this;
                server_callbacks_.auth_none_function = auth_none_callback;
                server_callbacks_.auth_pubkey_function = auth_pubkey_callback;
                server_callbacks_.channel_open_request_session_function = channel_open_callback;
                ssh_callbacks_init(&server_callbacks_);
                ssh_set_server_callbacks(session_, &server_callbacks_);

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
            // Auth, channel open, PTY, and shell are all handled by callbacks
            // Just drain any messages and reply default to unexpected ones
            ssh_message msg = ssh_message_get(session_);
            if (msg) {
                int type = ssh_message_type(msg);

                // Auth and channel open are handled by callbacks - don't touch them
                if (type == SSH_REQUEST_AUTH || type == SSH_REQUEST_CHANNEL_OPEN) {
                    ssh_message_free(msg);
                    break;
                }

                // Other messages get default reply
                ssh_message_reply_default(msg);
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

    // Handle any pending SSH messages (env vars, window changes, etc.)
    // These can arrive after shell is established
    ssh_message msg;
    while ((msg = ssh_message_get(session_)) != nullptr) {
        int type = ssh_message_type(msg);
        int subtype = ssh_message_subtype(msg);

        if (type == SSH_REQUEST_CHANNEL) {
            if (subtype == SSH_CHANNEL_REQUEST_ENV) {
                // Accept environment variables (we ignore them)
                ssh_message_channel_request_reply_success(msg);
            } else if (subtype == SSH_CHANNEL_REQUEST_WINDOW_CHANGE) {
                // Accept window size changes (we ignore them)
                ssh_message_channel_request_reply_success(msg);
            } else {
                ssh_message_reply_default(msg);
            }
        } else {
            ssh_message_reply_default(msg);
        }
        ssh_message_free(msg);
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
    static int poll_count = 0;
    poll_count++;
    if (poll_count <= 3) {
        std::cerr << "[SSH] poll_io #" << poll_count << " n=" << n << "\n";
    }
    if (n > 0) {
        std::cerr << "[SSH] Received " << n << " chars for console " << console_id_ << ": ";
        for (int i = 0; i < n; i++) {
            uint8_t ch = static_cast<uint8_t>(buf[i]);
            if (ch >= 32 && ch < 127) std::cerr << (char)ch;
            else std::cerr << "\\x" << std::hex << (int)ch << std::dec;
            con->input_queue().try_write(ch);
        }
        std::cerr << "\n";
    } else if (n == SSH_ERROR) {
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

    std::cerr << "[SSH] Loaded " << count << " authorized keys from " << path << "\n";
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

    // Got a connection - set non-blocking at both OS and libssh level
    socket_t fd = ssh_get_fd(session);
    if (fd != SSH_INVALID_SOCKET) {
        set_nonblocking(fd);
    }
    ssh_set_blocking(session, 0);  // Tell libssh we're non-blocking

    // Create session object - handshake will proceed in poll()
    sessions_.push_back(std::make_unique<SSHSession>(session, this));
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
