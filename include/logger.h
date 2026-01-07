// logger.h - Simple logging for MP/M II Emulator
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Thread-safe logger with timestamps. Logs to file and optionally stderr.

#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

class Logger {
public:
    static Logger& instance() {
        static Logger logger;
        return logger;
    }

    // Open log file. Returns false on error.
    bool open(const std::string& path) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }
        file_.open(path, std::ios::app);
        return file_.is_open();
    }

    // Close log file
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.close();
        }
    }

    // Log a message with timestamp
    // Format: 2025-01-06 12:34:56 [TYPE] message
    void log(const char* type, const std::string& message) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!file_.is_open()) return;

        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto tm = std::localtime(&time_t);

        file_ << std::put_time(tm, "%Y-%m-%d %H:%M:%S")
              << " [" << type << "] "
              << message << "\n";
        file_.flush();
    }

    // Convenience methods
    void http(const std::string& message) { log("HTTP", message); }
    void ssh(const std::string& message) { log("SSH", message); }
    void sftp(const std::string& message) { log("SFTP", message); }

private:
    Logger() = default;
    ~Logger() { close(); }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex mutex_;
    std::ofstream file_;
};

// Convenience macro for logging with client IP
#define LOG_HTTP(ip, msg) Logger::instance().http(std::string(ip) + " " + (msg))
#define LOG_SSH(ip, msg) Logger::instance().ssh(std::string(ip) + " " + (msg))
#define LOG_SFTP(ip, msg) Logger::instance().sftp(std::string(ip) + " " + (msg))
