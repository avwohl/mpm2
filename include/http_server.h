// http_server.h - Non-blocking HTTP server for MP/M II file access
//
// Read-only HTTP server sharing the SFTP bridge for Z80 file operations.
// Uses polling (non-blocking sockets), no threading.

#pragma once

#include "sftp_bridge.h"
#include "sftp_path.h"
#include "listen_address.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// Forward declaration
class HTTPConnection;

class HTTPServer {
public:
    HTTPServer() = default;
    ~HTTPServer();

    // Add a listener on address and port. Returns false on error.
    // Empty host means INADDR_ANY (all interfaces)
    // Can be called multiple times to add multiple listeners.
    bool start(const std::string& host, int port);
    bool start(int port) { return start("", port); }
    bool start(const ListenAddress& addr) { return start(addr.host, addr.port); }

    // Stop server and close all connections
    void stop();

    // Poll for new connections and handle existing ones.
    // Call this from main loop.
    void poll();

    // Check if server is running (has at least one listener)
    bool is_running() const { return !listeners_.empty(); }

    // Get listen addresses
    const std::vector<ListenAddress>& listen_addresses() const { return listen_addrs_; }

private:
    struct Listener {
        int fd;
        ListenAddress addr;
    };
    std::vector<Listener> listeners_;
    std::vector<ListenAddress> listen_addrs_;  // Cached for API
    std::vector<std::unique_ptr<HTTPConnection>> connections_;

    void poll_accept();
    void poll_connections();
};

class HTTPConnection {
public:
    enum class State {
        READING_REQUEST,    // Reading HTTP request
        LISTING_ROOT,       // Building root drive listing
        LISTING_DIR,        // Fetching directory from Z80
        READING_FILE,       // Reading file from Z80
        SENDING_RESPONSE,   // Sending HTTP response
        DONE                // Connection complete
    };

    HTTPConnection(int fd, const std::string& client_ip);
    ~HTTPConnection();

    // Poll this connection. Returns false when done/error.
    bool poll();

    // Check if connection is done
    bool is_done() const { return state_ == State::DONE; }

private:
    int fd_;
    std::string client_ip_;
    State state_ = State::READING_REQUEST;

    // Request parsing
    std::string request_buffer_;
    std::string method_;        // GET or HEAD
    std::string path_;          // URL path
    bool is_head_ = false;      // HEAD request (no body)

    // Parsed path info
    SftpPath parsed_path_;

    // Response building
    std::string response_buffer_;
    size_t response_offset_ = 0;

    // Directory entry for listings
    struct DirEntry {
        std::string filename;   // lowercase, with dot
        uint32_t size;
        uint8_t user;
    };

    // Z80 request state
    uint32_t pending_request_id_ = 0;
    bool search_first_ = true;  // First directory search?
    bool file_opened_ = false;  // File open request completed?

    // Accumulated data
    std::vector<uint8_t> file_data_;
    std::vector<DirEntry> dir_entries_;

    // Request handling
    bool read_request();
    bool parse_request();

    // Response generation
    void build_root_listing();
    void start_dir_listing();
    bool poll_dir_listing();
    void build_dir_response();
    void start_file_read();
    bool poll_file_read();
    void build_file_response();
    void build_error_response(int code, const std::string& message);
    bool send_response();

    // Helpers
    std::string get_content_type(const std::string& filename);
    bool is_text_file(const std::string& filename);
    std::string convert_eol(const std::vector<uint8_t>& data);
    std::string to_lowercase(const std::string& s);
};
