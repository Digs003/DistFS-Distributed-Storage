#pragma once
#include <atomic>
#include <ctime>
#include <iostream>
#include <string>

namespace distfs {

/// Lightweight one-liner verbose logger.
/// Enable with Logger::enable(); then call VLOG(tag, msg) macros.
/// Thread-safe: the atomic flag is checked and std::cout is line-buffered.
class Logger {
public:
    static Logger& instance() {
        static Logger inst;
        return inst;
    }

    void set_verbose(bool v) { verbose_.store(v, std::memory_order_relaxed); }
    bool is_verbose() const  { return verbose_.load(std::memory_order_relaxed); }

    /// Emit one log line: "[HH:MM:SS][tag] msg"
    void log(const char* tag, const std::string& msg) const {
        if (!is_verbose()) return;
        std::time_t t = std::time(nullptr);
        struct tm tm_buf {};
        localtime_r(&t, &tm_buf);
        char ts[10];
        std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm_buf);
        // Single << chain keeps the line atomic under line-buffered cout
        std::cout << '[' << ts << "][" << tag << "] " << msg << '\n';
    }

private:
    Logger() = default;
    std::atomic<bool> verbose_{false};
};

} // namespace distfs

// Convenience macros — zero cost when verbose is off (atomic load is cheap)
#define VLOG(tag, msg) \
    do { ::distfs::Logger::instance().log((tag), (msg)); } while (0)
