// listen_address.h - Listen address parsing for HTTP/SSH servers
// Part of MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef LISTEN_ADDRESS_H
#define LISTEN_ADDRESS_H

#include <string>
#include <vector>
#include <optional>

// Represents a listen address (IP + port)
struct ListenAddress {
    std::string host;  // Empty string means INADDR_ANY / in6addr_any
    int port;

    ListenAddress() : port(0) {}
    ListenAddress(int p) : port(p) {}
    ListenAddress(const std::string& h, int p) : host(h), port(p) {}

    // Format as string for display
    std::string to_string() const {
        if (host.empty()) {
            return std::to_string(port);
        } else if (host.find(':') != std::string::npos) {
            // IPv6 - bracket notation
            return "[" + host + "]:" + std::to_string(port);
        } else {
            return host + ":" + std::to_string(port);
        }
    }
};

// Parse a listen address string
// Formats:
//   "PORT"           -> ("", PORT)
//   "IP:PORT"        -> (IP, PORT) for IPv4
//   "[IPv6]:PORT"    -> (IPv6, PORT) for IPv6
//   "IP"             -> (IP, default_port) if no colon or bracketed
//
// Returns std::nullopt on parse error
std::optional<ListenAddress> parse_listen_address(const std::string& str, int default_port = 0);

#endif // LISTEN_ADDRESS_H
