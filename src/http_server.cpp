// http_server.cpp - Non-blocking HTTP server for MP/M II file access
//
// Read-only HTTP server sharing the SFTP bridge for Z80 file operations.
// Uses polling (non-blocking sockets), no threading.

#include "http_server.h"
#include "logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <iomanip>

// Set socket to non-blocking mode
static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

// ============================================================================
// HTTPServer implementation
// ============================================================================

HTTPServer::~HTTPServer() {
    stop();
}

bool HTTPServer::start(const std::string& host, int port) {
    if (port <= 0) return false;

    // Use getaddrinfo for IPv4/IPv6 support
    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;      // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;      // For wildcard IP address

    const char* node = host.empty() ? nullptr : host.c_str();
    std::string port_str = std::to_string(port);

    struct addrinfo* result;
    int rc = getaddrinfo(node, port_str.c_str(), &hints, &result);
    if (rc != 0) {
        std::cerr << "[HTTP] getaddrinfo() failed: " << gai_strerror(rc) << "\n";
        return false;
    }

    // Try each address until we successfully bind
    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next) {
        listen_fd_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (listen_fd_ < 0) continue;

        // Allow address reuse
        int opt = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // For IPv6, allow IPv4 connections too (dual-stack) unless specific host
        if (rp->ai_family == AF_INET6 && host.empty()) {
            int no = 0;
            setsockopt(listen_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no));
        }

        // Set non-blocking
        if (!set_nonblocking(listen_fd_)) {
            close(listen_fd_);
            listen_fd_ = -1;
            continue;
        }

        if (bind(listen_fd_, rp->ai_addr, rp->ai_addrlen) == 0) {
            // Success
            break;
        }

        close(listen_fd_);
        listen_fd_ = -1;
    }

    freeaddrinfo(result);

    if (listen_fd_ < 0) {
        std::cerr << "[HTTP] bind() failed: " << strerror(errno) << "\n";
        return false;
    }

    if (listen(listen_fd_, 10) < 0) {
        std::cerr << "[HTTP] listen() failed: " << strerror(errno) << "\n";
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    listen_addr_ = ListenAddress(host, port);
    return true;
}

void HTTPServer::stop() {
    connections_.clear();
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    listen_addr_ = ListenAddress();
}

void HTTPServer::poll() {
    if (listen_fd_ < 0) return;

    poll_accept();
    poll_connections();
}

void HTTPServer::poll_accept() {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            std::cerr << "[HTTP] accept() failed: " << strerror(errno) << "\n";
        }
        return;
    }

    if (!set_nonblocking(client_fd)) {
        std::cerr << "[HTTP] Failed to set client non-blocking\n";
        close(client_fd);
        return;
    }

    std::string client_ip = inet_ntoa(client_addr.sin_addr);
    connections_.push_back(std::make_unique<HTTPConnection>(client_fd, client_ip));
}

void HTTPServer::poll_connections() {
    // Poll all connections and remove completed ones
    connections_.erase(
        std::remove_if(connections_.begin(), connections_.end(),
                       [](const std::unique_ptr<HTTPConnection>& conn) {
                           return !conn->poll();
                       }),
        connections_.end());
}

// ============================================================================
// HTTPConnection implementation
// ============================================================================

HTTPConnection::HTTPConnection(int fd, const std::string& client_ip)
    : fd_(fd), client_ip_(client_ip) {}

HTTPConnection::~HTTPConnection() {
    if (fd_ >= 0) {
        close(fd_);
    }
}

bool HTTPConnection::poll() {
    switch (state_) {
        case State::READING_REQUEST:
            if (!read_request()) return false;
            if (state_ == State::READING_REQUEST) return true;  // Still reading
            break;

        case State::LISTING_ROOT:
            build_root_listing();
            state_ = State::SENDING_RESPONSE;
            break;

        case State::LISTING_DIR:
            if (!poll_dir_listing()) return false;
            if (state_ == State::LISTING_DIR) return true;  // Still waiting
            break;

        case State::READING_FILE:
            if (!poll_file_read()) return false;
            if (state_ == State::READING_FILE) return true;  // Still waiting
            break;

        case State::SENDING_RESPONSE:
            if (!send_response()) return false;
            if (state_ == State::SENDING_RESPONSE) return true;  // Still sending
            break;

        case State::DONE:
            return false;
    }

    return true;
}

bool HTTPConnection::read_request() {
    char buf[1024];
    ssize_t n = read(fd_, buf, sizeof(buf));

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;  // No data yet
        }
        std::cerr << "[HTTP] read() failed: " << strerror(errno) << "\n";
        state_ = State::DONE;
        return false;
    }

    if (n == 0) {
        // Connection closed
        state_ = State::DONE;
        return false;
    }

    request_buffer_.append(buf, n);

    // Check for end of headers (blank line)
    if (request_buffer_.find("\r\n\r\n") != std::string::npos ||
        request_buffer_.find("\n\n") != std::string::npos) {
        return parse_request();
    }

    // Limit request size
    if (request_buffer_.size() > 8192) {
        build_error_response(400, "Request too large");
        state_ = State::SENDING_RESPONSE;
    }

    return true;
}

bool HTTPConnection::parse_request() {
    // Parse first line: METHOD PATH HTTP/1.x
    std::istringstream iss(request_buffer_);
    std::string http_version;
    iss >> method_ >> path_ >> http_version;

    LOG_HTTP(client_ip_, method_ + " " + path_);

    // Validate method
    if (method_ != "GET" && method_ != "HEAD") {
        build_error_response(405, "Method not allowed");
        state_ = State::SENDING_RESPONSE;
        return true;
    }

    is_head_ = (method_ == "HEAD");

    // URL decode path (simple: just handle %20 for space)
    std::string decoded_path;
    for (size_t i = 0; i < path_.size(); i++) {
        if (path_[i] == '%' && i + 2 < path_.size()) {
            unsigned int hex = 0;
            if (sscanf(path_.c_str() + i + 1, "%2x", &hex) == 1) {
                decoded_path += static_cast<char>(hex);
                i += 2;
                continue;
            }
        }
        decoded_path += path_[i];
    }
    path_ = decoded_path;

    // Parse path - custom logic for HTTP:
    // /       -> root (list drives)
    // /A/     -> drive A, all users (user=-1)
    // /A.0/   -> drive A, user 0
    // /A/FILE.TXT -> drive A, search all users for file
    // /A.0/FILE.TXT -> drive A, user 0, FILE.TXT

    parsed_path_.drive = -1;
    parsed_path_.user = -1;
    parsed_path_.filename = "";

    if (path_.empty() || path_ == "/") {
        // Root
        state_ = State::LISTING_ROOT;
        return true;
    }

    std::string p = path_;
    if (p[0] == '/') p = p.substr(1);

    // Remove trailing slashes
    while (!p.empty() && p.back() == '/') p.pop_back();

    if (p.empty()) {
        state_ = State::LISTING_ROOT;
        return true;
    }

    // First component is drive or drive.user
    size_t slash = p.find('/');
    std::string drive_part = (slash != std::string::npos) ? p.substr(0, slash) : p;
    std::string rest = (slash != std::string::npos) ? p.substr(slash + 1) : "";

    // Parse drive letter (A-P, case insensitive)
    char drive_letter = std::toupper(static_cast<unsigned char>(drive_part[0]));
    if (drive_letter < 'A' || drive_letter > 'P') {
        build_error_response(404, "Invalid drive");
        state_ = State::SENDING_RESPONSE;
        return true;
    }
    parsed_path_.drive = drive_letter - 'A';

    // Check for .N user suffix (e.g., "A.5" or "a.15")
    size_t dot = drive_part.find('.');
    if (dot != std::string::npos && dot == 1) {
        std::string user_str = drive_part.substr(2);
        if (!user_str.empty()) {
            try {
                int user = std::stoi(user_str);
                if (user >= 0 && user <= 15) {
                    parsed_path_.user = user;
                }
            } catch (...) {
                // Invalid user number, leave as -1
            }
        }
    }
    // Note: Without .N suffix, user stays -1 (all users)

    // Rest is filename (convert to uppercase for CP/M)
    if (!rest.empty()) {
        parsed_path_.filename = rest;
        std::transform(parsed_path_.filename.begin(), parsed_path_.filename.end(),
                       parsed_path_.filename.begin(),
                       [](unsigned char c) { return std::toupper(c); });
    }

    // Decide what to do
    if (parsed_path_.filename.empty()) {
        // Directory listing
        start_dir_listing();
    } else {
        // File request
        start_file_read();
    }

    return true;
}

void HTTPConnection::build_root_listing() {
    auto drives = get_mounted_drives();

    std::ostringstream html;
    html << "<html><head><title>MP/M II</title></head>\n";
    html << "<body><h1>MP/M II Drives</h1>\n<pre>\n";

    for (int d : drives) {
        char letter = 'a' + d;
        html << "<a href=\"/" << letter << "/\">" << letter << ":</a>\n";
    }

    html << "</pre></body></html>\n";

    std::string body = html.str();

    std::ostringstream resp;
    resp << "HTTP/1.0 200 OK\r\n";
    resp << "Content-Type: text/html\r\n";
    resp << "Content-Length: " << body.size() << "\r\n";
    resp << "Connection: close\r\n";
    resp << "\r\n";
    if (!is_head_) {
        resp << body;
    }

    response_buffer_ = resp.str();
}

void HTTPConnection::start_dir_listing() {
    dir_entries_.clear();
    search_first_ = true;

    // Request directory listing from Z80
    SftpRequest req;
    req.type = SftpRequestType::DIR_SEARCH;
    req.drive = parsed_path_.drive;
    req.user = (parsed_path_.user >= 0) ? parsed_path_.user : 0;  // Start with user 0 for all-user search
    req.filename = "*.*";
    req.flags = 0;  // Search first

    pending_request_id_ = SftpBridge::instance().enqueue_request(req);
    state_ = State::LISTING_DIR;
}

bool HTTPConnection::poll_dir_listing() {
    auto reply = SftpBridge::instance().try_get_reply(pending_request_id_);
    if (!reply) {
        return true;  // Still waiting
    }

    bool more_data = reply->more_data;

    if (reply->status == SftpReplyStatus::OK && reply->data.size() >= 32) {
        // Parse 32-byte CP/M directory entries
        size_t offset = 0;
        while (offset + 32 <= reply->data.size()) {
            DirEntry entry;

            // Build filename
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

            entry.filename = name;
            entry.user = reply->data[offset + 0];

            // Calculate size from extent/record count
            uint8_t rc_byte = reply->data[offset + 15];
            uint8_t ex_byte = reply->data[offset + 12];
            entry.size = (ex_byte * 128 + rc_byte) * 128;

            // Filter by user if specific user requested
            if (parsed_path_.user < 0 || entry.user == parsed_path_.user) {
                dir_entries_.push_back(entry);
            }

            offset += 32;
        }
    }

    if (more_data) {
        // Request more entries
        SftpRequest req;
        req.type = SftpRequestType::DIR_SEARCH;
        req.drive = parsed_path_.drive;
        req.user = (parsed_path_.user >= 0) ? parsed_path_.user : 0;
        req.filename = "*.*";
        req.flags = 1;  // Search next

        pending_request_id_ = SftpBridge::instance().enqueue_request(req);
        return true;  // Continue waiting
    }

    // Done - build response
    build_dir_response();
    state_ = State::SENDING_RESPONSE;
    return true;
}

void HTTPConnection::build_dir_response() {
    std::ostringstream html;

    // Path for display
    std::string display_path = "/" + std::string(1, static_cast<char>('a' + parsed_path_.drive));
    if (parsed_path_.user >= 0) {
        display_path += "." + std::to_string(parsed_path_.user);
    }
    display_path += "/";

    html << "<html><head><title>" << display_path << "</title></head>\n";
    html << "<body><h1>Directory " << display_path << "</h1>\n<pre>\n";

    // Parent link
    html << "<a href=\"/\">../</a>\n";

    // Sort entries by name
    std::sort(dir_entries_.begin(), dir_entries_.end(),
              [](const DirEntry& a, const DirEntry& b) {
                  return a.filename < b.filename;
              });

    for (const auto& entry : dir_entries_) {
        // Build link path
        std::string link = "/" + std::string(1, static_cast<char>('a' + parsed_path_.drive));
        if (parsed_path_.user >= 0) {
            link += "." + std::to_string(parsed_path_.user);
        } else {
            // For all-users listing, include user in path
            link += "." + std::to_string(entry.user);
        }
        link += "/" + entry.filename;

        // Format: filename   size   [user]
        html << "<a href=\"" << link << "\">" << entry.filename << "</a>";

        // Padding
        int pad = 14 - entry.filename.length();
        if (pad > 0) html << std::string(pad, ' ');

        html << " " << std::setw(8) << entry.size;

        if (parsed_path_.user < 0) {
            // Show user number for all-users listing
            html << "  [user " << (int)entry.user << "]";
        }

        html << "\n";
    }

    html << "</pre></body></html>\n";

    std::string body = html.str();

    std::ostringstream resp;
    resp << "HTTP/1.0 200 OK\r\n";
    resp << "Content-Type: text/html\r\n";
    resp << "Content-Length: " << body.size() << "\r\n";
    resp << "Connection: close\r\n";
    resp << "\r\n";
    if (!is_head_) {
        resp << body;
    }

    response_buffer_ = resp.str();
}

void HTTPConnection::start_file_read() {
    file_data_.clear();
    file_opened_ = false;

    // Open file via RSP bridge
    SftpRequest req;
    req.type = SftpRequestType::FILE_OPEN;
    req.drive = parsed_path_.drive;
    req.user = (parsed_path_.user >= 0) ? parsed_path_.user : 0;  // Default to user 0 for file access
    req.filename = parsed_path_.filename;
    req.flags = 0;  // Read mode

    pending_request_id_ = SftpBridge::instance().enqueue_request(req);
    state_ = State::READING_FILE;
}

bool HTTPConnection::poll_file_read() {
    auto reply = SftpBridge::instance().try_get_reply(pending_request_id_);
    if (!reply) {
        return true;  // Still waiting
    }

    if (!file_opened_) {
        // This is the FILE_OPEN reply
        if (reply->status != SftpReplyStatus::OK) {
            build_error_response(404, "File not found");
            state_ = State::SENDING_RESPONSE;
            return true;
        }

        file_opened_ = true;

        // Now read file content
        SftpRequest req;
        req.type = SftpRequestType::FILE_READ;
        req.drive = parsed_path_.drive;
        req.user = (parsed_path_.user >= 0) ? parsed_path_.user : 0;
        req.filename = parsed_path_.filename;

        pending_request_id_ = SftpBridge::instance().enqueue_request(req);
        return true;
    }

    // This is a FILE_READ reply
    bool more_data = reply->more_data;

    if (reply->status == SftpReplyStatus::OK) {
        // Append data
        file_data_.insert(file_data_.end(), reply->data.begin(), reply->data.end());
    }

    if (more_data && reply->status == SftpReplyStatus::OK) {
        // Read more
        SftpRequest req;
        req.type = SftpRequestType::FILE_READ;
        req.drive = parsed_path_.drive;
        req.user = (parsed_path_.user >= 0) ? parsed_path_.user : 0;
        req.filename = parsed_path_.filename;

        pending_request_id_ = SftpBridge::instance().enqueue_request(req);
        return true;
    }

    // Done reading - close file (don't wait for reply)
    SftpRequest close_req;
    close_req.type = SftpRequestType::FILE_CLOSE;
    close_req.drive = parsed_path_.drive;
    close_req.user = (parsed_path_.user >= 0) ? parsed_path_.user : 0;
    close_req.filename = parsed_path_.filename;
    SftpBridge::instance().enqueue_request(close_req);

    // Build response
    build_file_response();
    state_ = State::SENDING_RESPONSE;
    return true;
}

void HTTPConnection::build_file_response() {
    std::string content_type = get_content_type(parsed_path_.filename);
    std::string body;

    if (is_text_file(parsed_path_.filename)) {
        // Convert EOL for text files
        body = convert_eol(file_data_);
    } else {
        // Binary file - as-is
        body.assign(reinterpret_cast<char*>(file_data_.data()), file_data_.size());
    }

    std::ostringstream resp;
    resp << "HTTP/1.0 200 OK\r\n";
    resp << "Content-Type: " << content_type << "\r\n";
    resp << "Content-Length: " << body.size() << "\r\n";
    resp << "Connection: close\r\n";
    resp << "\r\n";
    if (!is_head_) {
        resp << body;
    }

    response_buffer_ = resp.str();
}

void HTTPConnection::build_error_response(int code, const std::string& message) {
    std::string status_text;
    switch (code) {
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 500: status_text = "Internal Server Error"; break;
        default: status_text = "Error"; break;
    }

    std::ostringstream body;
    body << "<html><head><title>" << code << " " << status_text << "</title></head>\n";
    body << "<body><h1>" << code << " " << status_text << "</h1>\n";
    body << "<p>" << message << "</p></body></html>\n";

    std::string body_str = body.str();

    std::ostringstream resp;
    resp << "HTTP/1.0 " << code << " " << status_text << "\r\n";
    resp << "Content-Type: text/html\r\n";
    resp << "Content-Length: " << body_str.size() << "\r\n";
    resp << "Connection: close\r\n";
    resp << "\r\n";
    if (!is_head_) {
        resp << body_str;
    }

    response_buffer_ = resp.str();
}

bool HTTPConnection::send_response() {
    if (response_offset_ >= response_buffer_.size()) {
        state_ = State::DONE;
        return false;
    }

    ssize_t n = write(fd_,
                      response_buffer_.data() + response_offset_,
                      response_buffer_.size() - response_offset_);

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;  // Would block, try again later
        }
        std::cerr << "[HTTP] write() failed: " << strerror(errno) << "\n";
        state_ = State::DONE;
        return false;
    }

    response_offset_ += n;

    if (response_offset_ >= response_buffer_.size()) {
        state_ = State::DONE;
        return false;
    }

    return true;
}

std::string HTTPConnection::get_content_type(const std::string& filename) {
    // Find extension
    size_t dot = filename.rfind('.');
    if (dot == std::string::npos) {
        return "application/octet-stream";
    }

    std::string ext = filename.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    if (ext == ".TXT" || ext == ".ASM" || ext == ".PLM" || ext == ".MAC" ||
        ext == ".SUB" || ext == ".LIB" || ext == ".DOC" || ext == ".HLP") {
        return "text/plain; charset=utf-8";
    }
    if (ext == ".HTM" || ext == ".HTML") {
        return "text/html; charset=utf-8";
    }
    if (ext == ".BAS") {
        return "text/plain; charset=utf-8";
    }

    return "application/octet-stream";
}

bool HTTPConnection::is_text_file(const std::string& filename) {
    size_t dot = filename.rfind('.');
    if (dot == std::string::npos) {
        return false;
    }

    std::string ext = filename.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    return ext == ".TXT" || ext == ".ASM" || ext == ".PLM" || ext == ".MAC" ||
           ext == ".SUB" || ext == ".LIB" || ext == ".HTM" || ext == ".HTML" ||
           ext == ".DOC" || ext == ".HLP" || ext == ".BAS";
}

std::string HTTPConnection::convert_eol(const std::vector<uint8_t>& data) {
    std::string result;
    result.reserve(data.size());

    for (size_t i = 0; i < data.size(); i++) {
        uint8_t c = data[i];

        // Strip CP/M EOF marker
        if (c == 0x1A) break;

        // Handle CR
        if (c == 0x0D) {
            // Check if followed by LF
            if (i + 1 < data.size() && data[i + 1] == 0x0A) {
                // CR+LF -> LF
                result += '\n';
                i++;  // Skip the LF
            }
            // Otherwise strip lone CR
            continue;
        }

        result += static_cast<char>(c);
    }

    return result;
}

std::string HTTPConnection::to_lowercase(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}
