// listen_address.cpp - Listen address parsing implementation
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#include "listen_address.h"
#include <cctype>

std::optional<ListenAddress> parse_listen_address(const std::string& str, int default_port) {
    if (str.empty()) {
        return std::nullopt;
    }

    // Check for IPv6 bracket notation: [IPv6]:port
    if (str[0] == '[') {
        size_t close_bracket = str.find(']');
        if (close_bracket == std::string::npos) {
            return std::nullopt;  // Unclosed bracket
        }

        std::string host = str.substr(1, close_bracket - 1);
        if (host.empty()) {
            return std::nullopt;  // Empty brackets
        }

        // Check for port after bracket
        if (close_bracket + 1 < str.length()) {
            if (str[close_bracket + 1] != ':') {
                return std::nullopt;  // Expected colon after bracket
            }
            std::string port_str = str.substr(close_bracket + 2);
            if (port_str.empty()) {
                return std::nullopt;
            }
            // Parse port
            try {
                int port = std::stoi(port_str);
                if (port <= 0 || port > 65535) {
                    return std::nullopt;
                }
                return ListenAddress(host, port);
            } catch (...) {
                return std::nullopt;
            }
        } else {
            // No port specified, use default
            if (default_port <= 0) {
                return std::nullopt;  // Port required but not provided
            }
            return ListenAddress(host, default_port);
        }
    }

    // Check if it's just a port number (all digits)
    bool all_digits = true;
    for (char c : str) {
        if (!std::isdigit(c)) {
            all_digits = false;
            break;
        }
    }

    if (all_digits) {
        try {
            int port = std::stoi(str);
            if (port <= 0 || port > 65535) {
                return std::nullopt;
            }
            return ListenAddress("", port);
        } catch (...) {
            return std::nullopt;
        }
    }

    // Look for last colon (to handle IPv4:port)
    size_t last_colon = str.rfind(':');
    if (last_colon == std::string::npos) {
        // No colon - treat as host with default port
        if (default_port <= 0) {
            return std::nullopt;  // Port required but not provided
        }
        return ListenAddress(str, default_port);
    }

    // Check if there are multiple colons (could be IPv6 without brackets)
    size_t first_colon = str.find(':');
    if (first_colon != last_colon) {
        // Multiple colons - likely IPv6 without brackets
        // Treat whole string as host, use default port
        if (default_port <= 0) {
            return std::nullopt;  // Port required but not provided
        }
        return ListenAddress(str, default_port);
    }

    // Single colon - split into host:port
    std::string host = str.substr(0, last_colon);
    std::string port_str = str.substr(last_colon + 1);

    if (port_str.empty()) {
        // Trailing colon with no port
        if (default_port <= 0) {
            return std::nullopt;
        }
        return ListenAddress(host, default_port);
    }

    try {
        int port = std::stoi(port_str);
        if (port <= 0 || port > 65535) {
            return std::nullopt;
        }
        return ListenAddress(host, port);
    } catch (...) {
        return std::nullopt;
    }
}
