// ssh_test.cpp - Non-blocking libssh test using ssh_event API
// Echo control chars, increment printable chars
// Uses poll-based event handling for true non-blocking I/O

#include <libssh/libssh.h>
#include <libssh/server.h>
#include <libssh/callbacks.h>
#include <iostream>
#include <vector>
#include <memory>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>

// Set fd to non-blocking
static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

enum class SessionState {
    KEY_EXCHANGE,
    AUTHENTICATING,
    CHANNEL_OPEN,
    READY,
    CLOSED
};

struct Session {
    ssh_session session;
    ssh_channel channel;
    ssh_event event;
    SessionState state;
    bool kex_done;
    bool event_added;

    Session(ssh_session s) : session(s), channel(nullptr), event(nullptr),
                              state(SessionState::KEY_EXCHANGE), kex_done(false),
                              event_added(false) {
        // Create event context (but don't add session until after KEX)
        event = ssh_event_new();
    }

    void add_to_event() {
        if (event && !event_added) {
            ssh_event_add_session(event, session);
            event_added = true;
        }
    }

    ~Session() {
        if (event) {
            if (session && event_added) ssh_event_remove_session(event, session);
            ssh_event_free(event);
        }
        if (channel) {
            ssh_channel_close(channel);
            ssh_channel_free(channel);
        }
        if (session) {
            ssh_disconnect(session);
            ssh_free(session);
        }
    }

    // Poll this session's events (non-blocking)
    int poll() {
        if (!event) return SSH_ERROR;
        return ssh_event_dopoll(event, 0);  // 0 = don't block
    }
};

int main() {
    const int PORT = 2222;
    const char* HOST_KEY = "keys/ssh_host_rsa_key";

    // Unbuffered output
    std::cout.setf(std::ios::unitbuf);
    std::cerr.setf(std::ios::unitbuf);

    std::cout << "Non-blocking SSH test server (ssh_event API)\n";
    std::cout << "Echo control chars, increment printable chars\n";
    std::cout << "Connect with: ssh -p " << PORT << " user@localhost\n\n";

    // Create bind
    ssh_bind bind = ssh_bind_new();
    if (!bind) {
        std::cerr << "Failed to create ssh_bind\n";
        return 1;
    }

    // Set options
    if (ssh_bind_options_set(bind, SSH_BIND_OPTIONS_BINDPORT, &PORT) < 0) {
        std::cerr << "Failed to set port\n";
        return 1;
    }

    if (ssh_bind_options_set(bind, SSH_BIND_OPTIONS_HOSTKEY, HOST_KEY) < 0) {
        std::cerr << "Failed to set host key: " << ssh_get_error(bind) << "\n";
        std::cerr << "Generate with: ssh-keygen -t rsa -f " << HOST_KEY << " -N ''\n";
        return 1;
    }

    // Listen
    if (ssh_bind_listen(bind) < 0) {
        std::cerr << "Failed to listen: " << ssh_get_error(bind) << "\n";
        return 1;
    }

    // Set bind to non-blocking
    ssh_bind_set_blocking(bind, 0);

    // Get bind socket and set non-blocking
    socket_t bind_fd = ssh_bind_get_fd(bind);
    if (bind_fd != SSH_INVALID_SOCKET) {
        set_nonblocking(bind_fd);
        std::cout << "Bind socket fd " << bind_fd << " set non-blocking\n";
    }

    std::cout << "Listening on port " << PORT << " (non-blocking)\n";
    std::cout << "Press Ctrl+C to quit\n\n";

    std::vector<std::unique_ptr<Session>> sessions;
    int loop_count = 0;

    // Main poll loop
    while (true) {
        loop_count++;
        if (loop_count % 1000 == 0) {
            std::cout << "[LOOP] " << loop_count << " iterations, "
                      << sessions.size() << " sessions\n";
        }

        // Try to accept new connection (non-blocking)
        ssh_session new_session = ssh_new();
        if (new_session) {
            int rc = ssh_bind_accept(bind, new_session);
            if (rc == SSH_OK) {
                std::cout << "[ACCEPT] New connection\n";

                // Get the socket fd and make it non-blocking
                socket_t fd = ssh_get_fd(new_session);
                if (fd != SSH_INVALID_SOCKET) {
                    set_nonblocking(fd);
                    std::cout << "[ACCEPT] Set fd " << fd << " non-blocking\n";
                }

                sessions.push_back(std::make_unique<Session>(new_session));
            } else {
                ssh_free(new_session);
            }
        }

        // Process each session
        for (auto it = sessions.begin(); it != sessions.end(); ) {
            Session& s = **it;
            bool remove = false;

            // Poll for any pending events on this session (only after kex)
            if (s.kex_done) {
                int poll_rc = s.poll();
                if (poll_rc == SSH_ERROR) {
                    std::cout << "[POLL] Error: " << ssh_get_error(s.session) << "\n";
                    remove = true;
                }
            }

            if (!remove) {
                switch (s.state) {
                    case SessionState::KEY_EXCHANGE: {
                        // Try key exchange (will return SSH_AGAIN if not ready)
                        int rc = ssh_handle_key_exchange(s.session);
                        if (rc == SSH_OK) {
                            std::cout << "[KEX] Key exchange complete\n";
                            s.kex_done = true;
                            s.add_to_event();  // Now safe to add to event
                            s.state = SessionState::AUTHENTICATING;
                        } else if (rc == SSH_ERROR) {
                            std::cout << "[KEX] Failed: " << ssh_get_error(s.session) << "\n";
                            remove = true;
                        }
                        // SSH_AGAIN means still in progress
                        break;
                    }

                    case SessionState::AUTHENTICATING:
                    case SessionState::CHANNEL_OPEN: {
                        ssh_message msg = ssh_message_get(s.session);
                        if (msg) {
                            int type = ssh_message_type(msg);
                            int subtype = ssh_message_subtype(msg);

                            if (type == SSH_REQUEST_AUTH) {
                                std::cout << "[AUTH] Auth request, accepting\n";
                                ssh_message_auth_reply_success(msg, 0);
                                s.state = SessionState::CHANNEL_OPEN;
                            } else if (type == SSH_REQUEST_CHANNEL_OPEN) {
                                if (subtype == SSH_CHANNEL_SESSION) {
                                    s.channel = ssh_message_channel_request_open_reply_accept(msg);
                                    if (s.channel) {
                                        std::cout << "[CHAN] Channel opened\n";
                                    }
                                } else {
                                    ssh_message_reply_default(msg);
                                }
                            } else if (type == SSH_REQUEST_CHANNEL && s.channel) {
                                if (subtype == SSH_CHANNEL_REQUEST_PTY) {
                                    std::cout << "[CHAN] PTY request, accepting\n";
                                    ssh_message_channel_request_reply_success(msg);
                                } else if (subtype == SSH_CHANNEL_REQUEST_SHELL) {
                                    std::cout << "[CHAN] Shell request, accepting\n";
                                    ssh_message_channel_request_reply_success(msg);
                                    s.state = SessionState::READY;

                                    // Send welcome
                                    const char* welcome = "\r\nSSH Test - echo ctrl, increment printable\r\n> ";
                                    ssh_channel_write(s.channel, welcome, strlen(welcome));
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

                    case SessionState::READY: {
                        if (!s.channel || ssh_channel_is_closed(s.channel) ||
                            ssh_channel_is_eof(s.channel)) {
                            std::cout << "[CHAN] Channel closed\n";
                            remove = true;
                            break;
                        }

                        // Non-blocking read
                        char buf[256];
                        int n = ssh_channel_read_nonblocking(s.channel, buf, sizeof(buf), 0);
                        if (n > 0) {
                            // Process each character
                            for (int i = 0; i < n; i++) {
                                unsigned char ch = buf[i];
                                char out[16];
                                int outlen = 0;

                                if (ch == 0x03) {
                                    // Ctrl+C
                                    std::cout << "[IN] Ctrl+C, closing\n";
                                    remove = true;
                                    break;
                                } else if (ch == 0x0d) {
                                    // CR - echo CR+LF and prompt
                                    std::cout << "[IN] CR (enter)\n";
                                    out[outlen++] = '\r';
                                    out[outlen++] = '\n';
                                    out[outlen++] = '>';
                                    out[outlen++] = ' ';
                                } else if (ch < 0x20 || ch == 0x7f) {
                                    // Control char - echo as-is
                                    std::cout << "[IN] ctrl 0x" << std::hex << (int)ch
                                              << std::dec << "\n";
                                    out[outlen++] = ch;
                                } else {
                                    // Printable - increment
                                    unsigned char inc = ch + 1;
                                    std::cout << "[IN] '" << (char)ch << "' -> '"
                                              << (char)inc << "'\n";
                                    out[outlen++] = inc;
                                }

                                if (outlen > 0 && !remove) {
                                    ssh_channel_write(s.channel, out, outlen);
                                }
                            }
                        } else if (n == SSH_ERROR) {
                            std::cout << "[CHAN] Read error\n";
                            remove = true;
                        }
                        break;
                    }

                    case SessionState::CLOSED:
                        remove = true;
                        break;
                }
            }

            if (remove) {
                std::cout << "[SESSION] Removing session\n";
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }

        // Small sleep to avoid busy loop (1ms)
        usleep(1000);
    }

    ssh_bind_free(bind);
    return 0;
}
